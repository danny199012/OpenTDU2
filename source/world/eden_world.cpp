#include "shared.h"
#include "eden_world.h"

#include "config/gs_config.h"
#include "game/game_options.h"

#include <optional>
#include <algorithm>
#include <cfloat>
#include <unordered_set>
#include <SDL3/SDL_timer.h>

#include "filesystem/gs_file.h"
#include "render/gs_render.h"
#include "render/texture_cache.h"
#include "system/cmd_line.h"
#include "core/watchdog.h"
#include "core/xmb.h"
#include "core/crypto/cpr.h"
#include "render/eden/eden_weather.h"

namespace {
constexpr uint32_t kGridQuads = EdenWorld::kHeightfieldSize - 1;
// TD_backwash (shore foam) shader set; EdenRenderer sampler heap index of
// linear + mirrored repeat (eSamplerHeapIndex SH_LinearMirror).
constexpr uint64_t kBackwashShaderSet = 0x73078865ca31234dull;
constexpr uint16_t kSamplerLinearMirror = 4;
// SH_LinearUV + 3 * clamp + clamp (2DM / D3DTADDRESS 3 on u and v).
constexpr uint8_t kSamplerLinearClampUV = 5 + 3 * 2 + 2;
// TD_Ground, TD_Ground_HM, TD_Road shader sets (ShaderSet guids, FUN_00982af0
// in night_lighting_spec 2.1: the -s map on their SHADOW slots).
constexpr uint64_t kGroundShaderSet = 0xb0f1234b0f123473ull;
constexpr uint64_t kGroundHMShaderSet = 0xa0a0a1a1a2a2a3a3ull;
constexpr uint64_t kRoadShaderSet = 0xc8ad93c95455291bull;
const uint64_t kShadowSlotType = GetIdentifier64bit( "SHADOW" );
const uint64_t kSectorOffset = GetIdentifier64bit( "SECTROFF" );

std::string SectorName( const EdenWorld::SectorId &id ) {
    char name[64];
    snprintf( name, sizeof( name ), "sector-%d-%d-%d-%d", id.AreaX, id.AreaY, id.X, id.Y );
    return name;
}

// Tall grass switches (docs/re/grass_spec.md 6): -nograss off, -grassdist <m>
// the card radius around the camera, -grassmax <n> the per-frame card cap,
// -grassstride <n> grid cells between cards (0.75 m each), -grassflip the
// grid row order A/B, -loggrass the per-frame counts.
bool GrassEnabled() {
    static const bool b = !CmdLine::HasArg( "-nograss" );
    return b;
}

float GrassDistance() {
    static const float v = [] {
        const char *p = CmdLine::GetArgValue( "-grassdist" );
        return p != nullptr ? Max( 20.0f, static_cast<float>( atof( p ) ) ) : 140.0f;
    }();
    return v;
}

uint32_t GrassMaxCards() {
    static const uint32_t v = [] {
        const char *p = CmdLine::GetArgValue( "-grassmax" );
        return p != nullptr ? static_cast<uint32_t>( Max( 1, atoi( p ) ) ) : 9000u;
    }();
    return v;
}

int32_t GrassStride() {
    static const int32_t v = [] {
        const char *p = CmdLine::GetArgValue( "-grassstride" );
        return p != nullptr ? Max( 1, atoi( p ) ) : 3;
    }();
    return v;
}

bool GrassFlip() {
    static const bool b = CmdLine::HasArg( "-grassflip" );
    return b;
}

bool LogGrass() {
    static const bool b = CmdLine::HasArg( "-loggrass" );
    return b;
}

inline uint32_t GrassHash( uint32_t x, uint32_t y ) {
    uint32_t h = x * 374761393u + y * 668265263u;
    h = ( h ^ ( h >> 13 ) ) * 1274126177u;
    return h ^ ( h >> 16 );
}

inline uint16_t GrassFloatToHalf( float value ) {
    uint32_t bits;
    memcpy( &bits, &value, 4 );
    const uint32_t sign = ( bits >> 16 ) & 0x8000u;
    const int32_t exponent = static_cast<int32_t>( ( bits >> 23 ) & 0xFF ) - 127 + 15;
    const uint32_t mantissa = bits & 0x7FFFFFu;
    if ( exponent >= 31 ) {
        return static_cast<uint16_t>( sign | 0x7C00u );
    }
    if ( exponent <= 0 ) {
        if ( exponent < -10 ) {
            return static_cast<uint16_t>( sign );
        }
        const uint32_t m = ( mantissa | 0x800000u ) >> ( 1 - exponent );
        return static_cast<uint16_t>( sign | ( ( m + 0x1000u ) >> 13 ) );
    }
    return static_cast<uint16_t>( sign | ( static_cast<uint32_t>( exponent ) << 10 ) |
                                  ( ( mantissa + 0x1000u ) >> 13 ) );
}

// Sector row / column of the "brick" grid (getSectorAt), packed as
// row * 1024 + column: the key loadAround / unloadFar use.
int32_t SectorRowColumn( const EdenWorld::SectorId &id ) {
    const int32_t column = id.AreaX * static_cast<int32_t>( EdenWorld::kSectorsPerArea ) + id.X;
    const int32_t row = id.AreaY * static_cast<int32_t>( EdenWorld::kSectorsPerArea ) + id.Y;
    return row * 1024 + column;
}

// The tunnel / bridge keyword of a scene node (the -logtunnels rule plus the
// bridge spellings the island scan found: sector-1-2-6-5's BP_PONTA / PASSA
// beside the used car dealer), or nullptr. Names are French-authored data;
// 'tun' covers the TUNNEL / *_W_T pairs of the session-42 tunnel.
const char *CarveKeyword( const std::vector<SceneNode> &nodes, int32_t index ) {
    if ( index < 0 || index >= static_cast<int32_t>( nodes.size() ) ) {
        return nullptr;
    }
    const SceneNode &node = nodes[index];
    const int32_t parent = node.Parent;
    std::string lower = node.Name;
    std::transform( lower.begin(), lower.end(), lower.begin(), ::tolower );
    std::string parentName = parent >= 0 ? nodes[parent].Name : std::string();
    std::transform( parentName.begin(), parentName.end(), parentName.begin(), ::tolower );
    static const char *kKeywords[] = { "tun", "pont", "viad", "bridge" };
    for ( const char *keyword : kKeywords ) {
        if ( lower.find( keyword ) != std::string::npos || parentName.find( keyword ) != std::string::npos ) {
            return keyword;
        }
    }
    return nullptr;
}

// 8 m XZ bucket key of the carve ground (sector local cells).
uint64_t CellKey( int32_t x, int32_t z ) {
    return ( static_cast<uint64_t>( static_cast<uint32_t>( x ) ) << 32 ) | static_cast<uint32_t>( z );
}
} // namespace

// Marks the heightfield cells whose height lies inside a tunnel / bridge
// node's volume (world bbox + a one-cell margin). Their sheet crosses the
// object's shell and, drawn opaque with depth write ahead of it, fills the
// tunnel mouth with terrain (docs/STATE.md session 42; the session-56 island
// scan: 17 nodes / 12 sites on Ibiza). The marked quads move into a separate
// index buffer drawn first without depth write, so the tunnel / bridge
// geometry paints over them - the retail TD_HeightMap is itself blended
// without depth write, layer table 0x00f7f604 [CODE] - while every other
// cell keeps the opaque depth-writing draw (the session-22 see-through fix:
// hills keep hiding the buildings, roads and cars behind them).
// getHeight refuses the marked cells too: the drive physics must not ride
// the sheet through a bore (the car stood on the tunnel's road triangles).
void EdenWorld::MarkCarvedCells( Sector &sector ) {
    const std::vector<float> &heights = sector.Terrain.Heights;
    sector.Terrain.Carved.assign( kHeightfieldSize * kHeightfieldSize, 0 );
    if ( heights.empty() || !sector.Objects.isLoaded() ) {
        return;
    }
    const std::vector<SceneNode> &nodes = sector.Objects.getScene().getNodes();
    const float spacing = sector.Terrain.Spacing;
    uint32_t numMarked = 0;
    for ( int32_t i = 0; i < static_cast<int32_t>( nodes.size() ); i++ ) {
        if ( CarveKeyword( nodes, i ) == nullptr ) {
            continue;
        }
        Vector3f boundsMin, boundsMax;
        if ( !sector.Objects.getNodeBounds( i, boundsMin, boundsMax ) ) {
            continue;
        }
        const float x0 = boundsMin.x - spacing, x1 = boundsMax.x + spacing;
        const float z0 = boundsMin.z - spacing, z1 = boundsMax.z + spacing;
        const uint32_t gx0 = static_cast<uint32_t>( Clamp( x0 / spacing, 0.0f, 255.0f ) );
        const uint32_t gx1 = static_cast<uint32_t>( Clamp( x1 / spacing, 0.0f, 256.0f ) );
        const uint32_t gz0 = static_cast<uint32_t>( Clamp( z0 / spacing, 0.0f, 255.0f ) );
        const uint32_t gz1 = static_cast<uint32_t>( Clamp( z1 / spacing, 0.0f, 256.0f ) );
        for ( uint32_t gz = gz0; gz <= gz1; gz++ ) {
            for ( uint32_t gx = gx0; gx <= gx1; gx++ ) {
                const uint32_t cell = gz * kHeightfieldSize + gx;
                const float h = heights[cell];
                if ( h >= boundsMin.y - 0.25f && h <= boundsMax.y + 0.25f && sector.Terrain.Carved[cell] == 0 ) {
                    sector.Terrain.Carved[cell] = 1;
                    numMarked++;
                }
            }
        }
    }
    if ( numMarked != 0 ) {
        OTDU_LOG_DEBUG( "EdenWorld: %s: %u heightfield cells carved into tunnel / bridge volumes\n",
                        SectorName( sector.Id ).c_str(), numMarked );
    }
}

// Indexes the up-facing triangles INSIDE the tunnel / bridge node footprints
// into 8 m XZ buckets (sector local). getGroundHeight serves them to the drive
// physics where the heightfield is carved away: the sheet used to be the only
// ground there (the bore ramp the car climbed); the surfaces the player sees -
// the bore roads (their nodes are not keyword named, only the shells are) and
// the shell floors - are the ground the retail car stands on. Every item's
// triangles are tested against the carvers' footprints, not just the keyword
// nodes' own meshes (the cutting of sector-3-3-3-5 carries 513 triangles of
// shell, the bore road lives in the road nodes). Walls and ceilings are
// excluded by the up-normal test (a vertical triangle would evaluate to an
// arbitrary height at the query point).
void EdenWorld::BuildCarveGround( Sector &sector ) {
    sector.CarveTriangles.clear();
    sector.CarveCells.clear();
    if ( sector.Terrain.Carved.empty() || !sector.Objects.isLoaded() ) {
        return;
    }
    const std::vector<SceneNode> &nodes = sector.Objects.getScene().getNodes();
    // The carvers' footprints (sector local).
    std::vector<std::pair<Vector3f, Vector3f>> carveBoxes; // min, max
    for ( int32_t i = 0; i < static_cast<int32_t>( nodes.size() ); i++ ) {
        if ( CarveKeyword( nodes, i ) == nullptr ) {
            continue;
        }
        Vector3f boundsMin, boundsMax;
        if ( sector.Objects.getNodeBounds( i, boundsMin, boundsMax ) ) {
            carveBoxes.push_back( { boundsMin, boundsMax } );
        }
    }
    if ( carveBoxes.empty() ) {
        return;
    }
    const std::vector<EdenModel::Item> &items = sector.Objects.getItems();
    const std::vector<Vector3f> &soup = sector.Objects.getCollisionVertices();
    const std::vector<uint32_t> &soupIndices = sector.Objects.getCollisionIndices();
    constexpr float kCellSize = 8.0f;
    uint32_t numTriangles = 0;
    for ( const EdenModel::Item &item : items ) {
        if ( item.CollisionCount < 3 ) {
            continue;
        }
        // Quick reject on the node's world (sector local) bounds.
        Vector3f nodeMin, nodeMax;
        if ( !sector.Objects.getNodeBounds( item.NodeIndex, nodeMin, nodeMax ) ) {
            continue;
        }
        bool bOverlaps = false;
        for ( const auto &box : carveBoxes ) {
            if ( nodeMin.x <= box.second.x && nodeMax.x >= box.first.x && nodeMin.z <= box.second.z &&
                 nodeMax.z >= box.first.z ) {
                bOverlaps = true;
                break;
            }
        }
        if ( !bOverlaps ) {
            continue;
        }
        for ( uint32_t i = 0; i + 2 < item.CollisionCount; i += 3 ) {
            const uint32_t indexA = soupIndices[item.CollisionFirst + i];
            const uint32_t indexB = soupIndices[item.CollisionFirst + i + 1];
            const uint32_t indexC = soupIndices[item.CollisionFirst + i + 2];
            if ( indexA >= soup.size() || indexB >= soup.size() || indexC >= soup.size() ) {
                continue;
            }
            const Vector3f &a = soup[indexA];
            const Vector3f &b = soup[indexB];
            const Vector3f &c = soup[indexC];
            const float minX = Min( a.x, Min( b.x, c.x ) ), maxX = Max( a.x, Max( b.x, c.x ) );
            const float minZ = Min( a.z, Min( b.z, c.z ) ), maxZ = Max( a.z, Max( b.z, c.z ) );
            bool bInside = false;
            for ( const auto &box : carveBoxes ) {
                if ( maxX >= box.first.x && minX <= box.second.x && maxZ >= box.first.z && minZ <= box.second.z ) {
                    bInside = true;
                    break;
                }
            }
            if ( !bInside ) {
                continue;
            }
            const Vector3f normal = ( b - a ).cross( c - a );
            const float length = normal.length();
            if ( length < 1e-6f || normal.y <= 0.5f * length ) {
                continue; // not walkable from above (walls, ceilings)
            }
            const int32_t cellX0 = static_cast<int32_t>( std::floor( minX / kCellSize ) );
            const int32_t cellX1 = static_cast<int32_t>( std::floor( maxX / kCellSize ) );
            const int32_t cellZ0 = static_cast<int32_t>( std::floor( minZ / kCellSize ) );
            const int32_t cellZ1 = static_cast<int32_t>( std::floor( maxZ / kCellSize ) );
            if ( ( cellX1 - cellX0 ) > 64 || ( cellZ1 - cellZ0 ) > 64 ) {
                continue; // degenerate
            }
            const uint32_t slot = static_cast<uint32_t>( sector.CarveTriangles.size() / 3 ); // triangle ordinal
            sector.CarveTriangles.push_back( indexA );
            sector.CarveTriangles.push_back( indexB );
            sector.CarveTriangles.push_back( indexC );
            for ( int32_t cz = cellZ0; cz <= cellZ1; cz++ ) {
                for ( int32_t cx = cellX0; cx <= cellX1; cx++ ) {
                    sector.CarveCells[CellKey( cx, cz )].push_back( slot );
                }
            }
            numTriangles++;
        }
    }
    if ( numTriangles != 0 ) {
        OTDU_LOG_INFO( "EdenWorld: %s: %u carve-ground triangles indexed (%zu buckets)\n", SectorName( sector.Id ).c_str(),
                       numTriangles, sector.CarveCells.size() );
    }
}

// Topmost up-facing carve-ground surface at (x, z) below fromY (world).
bool EdenWorld::SampleCarveGround( const Sector &sector, float x, float z, float fromY, float &outHeight ) {
    if ( sector.CarveTriangles.empty() ) {
        return false;
    }
    // The collision soup is SECTOR-LOCAL: EdenModel::load builds it from the
    // node transforms (node.World.transformPoint), and the sector is drawn with
    // a Translation(Origin) on top of that. The bucket key has always been
    // local; the point test below was not - it compared the WORLD query (the
    // height function is called with world x / z) against the LOCAL vertices,
    // so it matched nothing on any sector with a non-zero origin, i.e. on every
    // tunnel / bridge of the island. The drive collision asks this through
    // getGroundHeight over exactly the carved cells (the sheet is not ground
    // there any more), so those cells answered "no ground": the owner's "holes
    // in the geometry ... i thought it was transparent i fell through ... near
    // tunnels/bridges" (session 86; -scantunnels' carve coverage measured 10209
    // of 10209 carved cells uncovered before this).
    const float localX = x - sector.Origin.x;
    const float localZ = z - sector.Origin.z;
    const auto &it = sector.CarveCells.find( CellKey( static_cast<int32_t>( std::floor( localX / 8.0f ) ),
                                                      static_cast<int32_t>( std::floor( localZ / 8.0f ) ) ) );
    if ( it == sector.CarveCells.end() ) {
        return false;
    }
    const std::vector<Vector3f> &soup = sector.Objects.getCollisionVertices();
    bool bFound = false;
    float best = -FLT_MAX;
    for ( uint32_t slot : it->second ) {
        const uint32_t indexA = sector.CarveTriangles[slot * 3 + 0];
        const uint32_t indexB = sector.CarveTriangles[slot * 3 + 1];
        const uint32_t indexC = sector.CarveTriangles[slot * 3 + 2];
        if ( indexA >= soup.size() || indexB >= soup.size() || indexC >= soup.size() ) {
            continue;
        }
        const Vector3f &a = soup[indexA];
        const Vector3f &b = soup[indexB];
        const Vector3f &c = soup[indexC];
        if ( localX < Min( a.x, Min( b.x, c.x ) ) || localX > Max( a.x, Max( b.x, c.x ) ) ||
             localZ < Min( a.z, Min( b.z, c.z ) ) || localZ > Max( a.z, Max( b.z, c.z ) ) ) {
            continue;
        }
        // Barycentric coordinates in the XZ plane (EdenModel::getSurfaceHeight).
        const float denominator = ( b.z - c.z ) * ( a.x - c.x ) + ( c.x - b.x ) * ( a.z - c.z );
        if ( std::abs( denominator ) < 1e-8f ) {
            continue;
        }
        const float u = ( ( b.z - c.z ) * ( localX - c.x ) + ( c.x - b.x ) * ( localZ - c.z ) ) / denominator;
        const float v = ( ( c.z - a.z ) * ( localX - c.x ) + ( a.x - c.x ) * ( localZ - c.z ) ) / denominator;
        const float w = 1.0f - u - v;
        if ( u < 0.0f || v < 0.0f || w < 0.0f ) {
            continue;
        }
        const float y = u * a.y + v * b.y + w * c.y + sector.Origin.y;
        // A little above the query origin still counts: a wheel that dipped a
        // few centimetres under the floor between two frames re-catches (the
        // raycast's t = 0 rule).
        if ( y <= fromY + 0.25f && y > best ) {
            best = y;
            bFound = true;
        }
    }
    if ( bFound ) {
        outHeight = best;
    }
    return bFound;
}

EdenWorld::EdenWorld()
    : pRenderer( nullptr ),
      areaColumns( 7 ),
      areaRows( 6 ),
      pGridBuffer( nullptr ),
      pGridIndices( nullptr ),
      numGridIndices( 0 ) {}

EdenWorld::~EdenWorld() { destroy(); }

bool EdenWorld::initialize( EdenRenderer &renderer, const char *pIsland ) {
    destroy();
    pRenderer = &renderer;
    islandFolder = "Euro/Bnk/islands/" + std::string( pIsland ) + "/level/";
    if ( !loadWorldConfig( pIsland ) ) {
        // Ibiza values (its worldconfig.cpr) [DATA].
        areaColumns = 7;
        areaRows = 6;
        OTDU_LOG_WARN( "EdenWorld: %sworldconfig.cpr not readable, %d x %d areas\n", islandFolder.c_str(), areaColumns,
                       areaRows );
    }

    // Shared heightfield grid: TEXCOORD0 = (x, z) grid coordinates (the
    // TD_HeightMap vertex shader scales them by the 3 m spacing).
    std::vector<float> grid;
    grid.reserve( kHeightfieldSize * kHeightfieldSize * 2 );
    for ( uint32_t z = 0; z < kHeightfieldSize; z++ ) {
        for ( uint32_t x = 0; x < kHeightfieldSize; x++ ) {
            grid.push_back( static_cast<float>( x ) );
            grid.push_back( static_cast<float>( z ) );
        }
    }
    std::vector<uint32_t> indices;
    indices.reserve( kGridQuads * kGridQuads * 6 );
    for ( uint32_t z = 0; z < kGridQuads; z++ ) {
        for ( uint32_t x = 0; x < kGridQuads; x++ ) {
            const uint32_t i0 = z * kHeightfieldSize + x;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + kHeightfieldSize;
            const uint32_t i3 = i2 + 1;
            // Winding: the heightfield materials cull D3DCULL_CW.
            indices.insert( indices.end(), { i0, i2, i1, i1, i2, i3 } );
        }
    }
    RenderDevice &device = gpRender->getDevice();
    pGridBuffer = device.createBuffer( grid.size() * sizeof( float ), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, grid.data() );
    pGridIndices = device.createBuffer( indices.size() * sizeof( uint32_t ), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                        indices.data() );
    numGridIndices = static_cast<uint32_t>( indices.size() );

    // Shared library (models instanced by the .pmi lists, detail textures).
    const std::string commonPath = islandFolder + "commonworld.bnk";
    if ( commonBank.load( commonPath.c_str() ) ) {
        gpRender->getTextureCache().loadBankTextures( commonBank, commonPath.c_str() );
        const BankEntry *pLibrary = commonBank.findEntry( "library", "2dm" );
        if ( pLibrary != nullptr ) {
            libraryMaterials.parse( pLibrary->pData, pLibrary->Size );
        }
        // Library models (instanced by the V / O .pmi lists): a gallery
        // scene whose node hashcodes are the MOB. hashcodes. The INS. LOD
        // geometry is loaded too: drawInstances switches level by distance
        // (docs/re/pmi_streaming_spec.md 9 #3).
        libraryModel.setLoadLods( !CmdLine::HasArg( "-nolodgeometry" ) );
        if ( libraryModel.load( renderer, commonBank, "library", &libraryMaterials, commonPath.c_str() ) ) {
            size_t lodItemCount[3] = { 0, 0, 0 };
            for ( const EdenModel::Item &item : libraryModel.getItems() ) {
                lodItemCount[item.Lod < 3 ? item.Lod : 0]++;
            }
            OTDU_LOG_DEBUG( "EdenWorld: library %zu items (LOD0 %zu, LOD1 %zu, LOD2 %zu)\n", libraryModel.getItems().size(),
                            lodItemCount[0], lodItemCount[1], lodItemCount[2] );
            const std::vector<SceneNode> &nodes = libraryModel.getScene().getNodes();
            for ( int32_t i = 0; i < static_cast<int32_t>( nodes.size() ); i++ ) {
                if ( nodes[i].ObjectHashcode != 0 ) {
                    libraryNodes.emplace( nodes[i].Hashcode, i );
                }
            }
            // The library materials are shared by every instance: the neon
            // set value is written in place (neon_spec 2).
            if ( !CmdLine::HasArg( "-noworldneons" ) ) {
                for ( EdenMaterial &material : libraryModel.getMaterials() ) {
                    if ( EdenNeons::IsNeonSetMaterial( material ) ) {
                        sharedNeonMaterials.push_back( &material );
                    }
                }
            }
        }
    } else {
        OTDU_LOG_WARN( "EdenWorld: '%s' not found\n", commonPath.c_str() );
    }

    // Tall grass (grass_spec.md): fx/tallgrass.bnk + grasstypes.xml.
    if ( GrassEnabled() ) {
        loadGrassAssets( pIsland );
    }

    // Island ocean (far sea, ocean_spec 5).
    const std::string islandPath = islandFolder + pIsland + ".bnk";
    if ( islandBank.load( islandPath.c_str() ) ) {
        gpRender->getTextureCache().loadBankTextures( islandBank, islandPath.c_str() );
        islandOcean.load( renderer, islandBank, ( std::string( pIsland ) + "-o" ).c_str(), &libraryMaterials,
                          islandPath.c_str() );
        pIslandColorMap = gpRender->getTextureCache().find( islandPath.c_str(), ( std::string( pIsland ) + "-c" ).c_str() );
        OTDU_LOG_INFO( "EdenWorld: island ocean bounds (%.0f %.0f %.0f) - (%.0f %.0f %.0f)\n", islandOcean.getBoundsMin().x,
                       islandOcean.getBoundsMin().y, islandOcean.getBoundsMin().z, islandOcean.getBoundsMax().x,
                       islandOcean.getBoundsMax().y, islandOcean.getBoundsMax().z );
        // Island level heightfield (<island>.3dd HMAP: 16 x 16 patches, 384 m
        // spacing [DATA]); the TD_HeightMap island permutation (S_USERTYPE4
        // = 2) scales the grid by 384 m [SHADER].
        if ( loadHeightfield( islandBank, pIsland, islandPath.c_str(), islandTerrain ) ) {
            islandTerrain.Spacing = 384.0f;
            islandTerrain.Materials[0].HeightfieldGrid = 2;
            renderer.updateProgram( islandTerrain.Materials[0] );
            if ( islandTerrain.Materials[0].pProgram == nullptr ) {
                OTDU_LOG_WARN( "EdenWorld: no island permutation of TD_HeightMap for '%s'\n", pIsland );
                islandTerrain.Materials.clear();
            }
        }
        bIslandIndicesDirty = true;
    }

    // Wet ground textures.
    const std::string weatherFxPath = "Euro/Bnk/fx/weatherfx.bnk";
    if ( weatherFxBank.load( weatherFxPath.c_str() ) ) {
        TextureCache &textures = gpRender->getTextureCache();
        textures.loadBankTextures( weatherFxBank, weatherFxPath.c_str() );
        for ( uint32_t i = 0; i < 16; i++ ) {
            char name[32];
            snprintf( name, sizeof( name ), "ripple_%02u", i + 1 );
            pRipples[i] = textures.find( weatherFxPath.c_str(), name );
        }
        pPuddles = textures.find( weatherFxPath.c_str(), "puddles" );
    }

    // Direction-sign faces: composed from the payloads + the blank plates of
    // the rs_maps atlas (sign_text.cpp); needs the frontend font when present.
    signFaces.initialize( commonBank.findEntry( "rs_maps", "2db" ) );

    // Actors of the A .pmi lists (road signs...): Entity__Actor (__Hc,
    // __Name) of entities.xmb, the root node of the actor .3dd is the
    // identifier of __Name.
    const std::string actorPath = "Euro/Bnk/islands/" + std::string( pIsland ) + "/scenarics/packs/worldactors.bnk";
    if ( actorBank.load( actorPath.c_str() ) ) {
        gpRender->getTextureCache().loadBankTextures( actorBank, actorPath.c_str() );
        const BankEntry *pEntities = actorBank.findEntry( "entities.xmb" );
        XmbDocument entities;
        if ( pEntities != nullptr && entities.load( pEntities->pData, pEntities->Size ) ) {
            XmbValue root = entities.getRoot()["ExportType_Entity"];
            for ( uint32_t i = 0; i < root.getArraySize( "ArrayOf_Entity__Actor" ); i++ ) {
                XmbValue actor = root.getArrayElement( "ArrayOf_Entity__Actor", i );
                actorNames[actor["__Hc"].asUInt64()] = actor["__Name"].asString();
            }
        }
        for ( const BankEntry *pEntry : actorBank.findEntriesByExtension( "3dd" ) ) {
            Render3DD scene;
            if ( scene.parse( pEntry->pData, pEntry->Size, false ) && !scene.getNodes().empty() ) {
                actorScenes.emplace( scene.getNodes()[0].Hashcode, pEntry );
            }
        }
        OTDU_LOG_INFO( "EdenWorld: %zu world actors, %zu actor scenes\n", actorNames.size(), actorScenes.size() );
    } else {
        OTDU_LOG_WARN( "EdenWorld: '%s' not found (no road signs)\n", actorPath.c_str() );
    }
    return pGridBuffer != nullptr && pGridIndices != nullptr;
}

bool EdenWorld::loadWorldConfig( const char *pIsland ) {
    // <island>/level/worldconfig.cpr: encrypted XML, <Area NbColumns="7"
    // NbRows= "6"/> (Ibiza 7 x 6, Hawai and Casino 11 x 9) [DATA].
    std::string text;
    if ( !LoadCPRText( ( islandFolder + "worldconfig.cpr" ).c_str(), text ) ) {
        return false;
    }
    auto readAttribute = [&text]( const char *pName, int32_t &outValue ) {
        size_t position = text.find( pName );
        if ( position == std::string::npos ) {
            return false;
        }
        position = text.find( '"', position );
        if ( position == std::string::npos ) {
            return false;
        }
        outValue = atoi( text.c_str() + position + 1 );
        return outValue > 0;
    };
    int32_t columns = 0, rows = 0;
    if ( !readAttribute( "NbColumns", columns ) || !readAttribute( "NbRows", rows ) ) {
        return false;
    }
    areaColumns = columns;
    areaRows = rows;
    OTDU_LOG_INFO( "EdenWorld: %s: %d x %d areas (worldconfig.cpr)\n", pIsland, areaColumns, areaRows );
    return true;
}

std::string EdenWorld::getSectorBankPath( const SectorId &id ) const {
    char path[256];
    snprintf( path, sizeof( path ), "%sarea-%d/area-%d-%d/%s.bnk", islandFolder.c_str(), id.AreaX, id.AreaX, id.AreaY,
              SectorName( id ).c_str() );
    return path;
}

std::string EdenWorld::GetSectorBankPath( const char *pIsland, const SectorId &id ) {
    char path[256];
    snprintf( path, sizeof( path ), "Euro/Bnk/islands/%s/level/area-%d/area-%d-%d/%s.bnk", pIsland, id.AreaX, id.AreaX,
              id.AreaY, SectorName( id ).c_str() );
    return path;
}

int32_t EdenWorld::getActorModel( uint64_t hashcode ) {
    auto it = actorModelIndices.find( hashcode );
    if ( it != actorModelIndices.end() ) {
        return it->second;
    }
    int32_t index = -1;
    auto nameIt = actorNames.find( hashcode );
    if ( nameIt != actorNames.end() ) {
        auto sceneIt = actorScenes.find( GetIdentifier64bit( nameIt->second.c_str() ) );
        if ( sceneIt != actorScenes.end() ) {
            const BankEntry *pScene = sceneIt->second;
            const BankEntry *pGeometry = actorBank.findEntry( pScene->Name.c_str(), "3dg" );
            const BankEntry *pMaterials = actorBank.findEntry( pScene->Name.c_str(), "2dm" );
            auto pModel = std::make_unique<EdenModel>();
            const std::string modelName = ToLower( pScene->Name );
            // Road-sign actor models: read the plate layout before the model is
            // built, and let the geometry hook give stacked plates their own
            // atlas bar (the retail per-plate UV rewrite). The family (the
            // model-name prefix) decides the stamp orientation and whether the
            // payload's distance bytes are drawn (sign_text.h).
            SignFamily family = SignFamily::Unknown;
            if ( modelName.rfind( "rs_dir", 0 ) == 0 ) {
                family = SignFamily::Dir;
            } else if ( modelName.rfind( "rs_dist", 0 ) == 0 ) {
                family = SignFamily::Dist;
            } else if ( modelName.rfind( "rs_tdirib", 0 ) == 0 ) {
                family = SignFamily::Tdir;
            } else if ( modelName.rfind( "rs_hwdist", 0 ) == 0 ) {
                family = SignFamily::Hw;
            } else if ( modelName.rfind( "rs_twn", 0 ) == 0 ) {
                family = SignFamily::Twn;
            }
            SignLayout signLayout;
            if ( pGeometry != nullptr && family != SignFamily::Unknown ) {
                if ( IsSignTextLogging() ) {
                    OTDU_LOG_INFO( "SignText: plate layout of '%s'\n", modelName.c_str() );
                }
                signLayout = BuildSignLayout( pGeometry->pData, pGeometry->Size, family );
            }
            const EdenModel::GeometrySetup geometrySetup = [&signLayout]( Render3DG &geometry ) {
                RemapStackedPlateRows( geometry, signLayout );
            };
            if ( pGeometry != nullptr &&
                 pModel->load( *pRenderer, *pScene, *pGeometry, pMaterials, &libraryMaterials,
                               actorBank.getPath().c_str(), nullptr, nullptr, geometrySetup ) ) {
                // Actor materials are shared by every instance of the actor.
                if ( !CmdLine::HasArg( "-noworldneons" ) ) {
                    for ( EdenMaterial &material : pModel->getMaterials() ) {
                        if ( EdenNeons::IsNeonSetMaterial( material ) ) {
                            sharedNeonMaterials.push_back( &material );
                        }
                    }
                }
                ActorSignInfo signInfo;
                // Every actor whose plate material samples the shared rs_maps
                // atlas gets the composed-texture swap (sign_text.cpp): the
                // direction signs (rs_dir*), but also the TdirIb* panels, the
                // distance plates (rs_dist*, rs_hwdist*) and the town plates
                // (rs_twnin/twnout). Their type-4 payload decides whether there
                // is text; a model with no plate zone keeps SlotIndex -1.
                for ( uint32_t m = 0; m < pModel->getMaterials().size() && signInfo.SlotIndex < 0; m++ ) {
                    for ( uint32_t s = 0; s < 16 && signInfo.SlotIndex < 0; s++ ) {
                        if ( pModel->getMaterials()[m].TextureHashes[s] == GetIdentifier64bit( "rs_maps" ) ) {
                            signInfo.MaterialIndex = static_cast<int32_t>( m );
                            signInfo.SlotIndex = static_cast<int32_t>( s );
                        }
                    }
                }
                if ( signInfo.SlotIndex >= 0 ) {
                    // "<family>_<variant>_<hash>": the token before the hash
                    // is the variant letter (families contain underscores:
                    // rs_dirl, rs_tdirib, rs_hwdist).
                    std::vector<std::string> tokens;
                    size_t tokenStart = 0;
                    while ( tokenStart <= modelName.size() ) {
                        const size_t tokenEnd = modelName.find( '_', tokenStart );
                        tokens.push_back( modelName.substr( tokenStart, tokenEnd - tokenStart ) );
                        if ( tokenEnd == std::string::npos ) {
                            break;
                        }
                        tokenStart = tokenEnd + 1;
                    }
                    if ( tokens.size() >= 2 && tokens[tokens.size() - 2].size() == 1 ) {
                        signInfo.Variant = tokens[tokens.size() - 2][0];
                    }
                    signInfo.Layout = signLayout;
                    if ( signInfo.Layout.Quads.empty() ) {
                        // Not a plate sign after all (STOP, speed discs...):
                        // leave its baked atlas face alone.
                        if ( IsSignTextLogging() ) {
                            OTDU_LOG_INFO( "SignText: '%s' (actor %s) has no plate zone\n", pScene->Name.c_str(),
                                           nameIt->second.c_str() );
                        }
                        signInfo.SlotIndex = -1;
                    }
                }
                if ( IsSignTextLogging() && signInfo.SlotIndex >= 0 ) {
                    OTDU_LOG_INFO( "SignText: '%s' (actor %s) variant %c slot %d: %zu plate row zone(s)\n",
                                   pScene->Name.c_str(), nameIt->second.c_str(),
                                   signInfo.Variant != 0 ? signInfo.Variant : '?', signInfo.SlotIndex,
                                   signInfo.Layout.Quads.size() );
                    for ( const SignQuadLayout &quad : signInfo.Layout.Quads ) {
                        OTDU_LOG_INFO( "SignText:   zone u %.3f..%.3f  v %.3f..%.3f  top %.2f\n", quad.U0, quad.U1,
                                       quad.V0, quad.V1, quad.Top );
                    }
                }
                actorSignInfo.push_back( signInfo );
                actorModels.push_back( std::move( pModel ) );
                index = static_cast<int32_t>( actorModels.size() - 1 );
            }
        }
        if ( index < 0 ) {
            OTDU_LOG_WARN( "EdenWorld: actor '%s' has no model\n", nameIt->second.c_str() );
        }
    }
    actorModelIndices[hashcode] = index;
    return index;
}

void EdenWorld::loadInstances( Sector &sector, const std::string &name ) {
    // V (vegetation) and O (objects): static library models; S: skinned static
    // library models with a per instance bone palette (the guard rails
    // SF_GLISS / SF_GLISO); A: actors (road signs); F: empty in every sector
    // checked (docs/re/pmi_spec.md 0).
    for ( const uint8_t listKind0 : { 0u, 1u, 2u, 3u } ) {
        const char *pSuffix = listKind0 == 0 ? "-v" : listKind0 == 1 ? "-o" : listKind0 == 2 ? "-s" : "-a";
        const BankEntry *pEntry = sector.SectorBank.findEntry( ( name + pSuffix ).c_str(), "pmi" );
        if ( pEntry == nullptr ) {
            continue;
        }
        std::vector<PmiInstance> instances;
        if ( !ParsePmi( pEntry->pData, pEntry->Size, sector.Origin, instances ) ) {
            OTDU_LOG_WARN( "EdenWorld: failed to parse '%s'\n", pEntry->Filename.c_str() );
            continue;
        }
        for ( PmiInstance &instance : instances ) {
            int32_t model = -1;
            float distance = 2000.0f;
            if ( instance.Type == 1 || instance.Type == 3 ) {
                auto it = libraryNodes.find( instance.Hashcode );
                if ( it == libraryNodes.end() ) {
                    continue;
                }
                model = it->second;
                // LOD switch values (squared distances, x (16/9)^2: FUN_00da0440);
                // the instance is not drawn beyond the last one. The low LODs
                // (TD_HardwareInstance / TD_Impostor) are not loaded: LOD 0 is
                // drawn up to there.
                // FUN_00989d10 / FUN_00df6e60: d^2 < lod * 3.160494 * F, F =
                // max(SET_GLOBAL_LOD_FACTOR, 1) for the last LOD; a model
                // without LOD switching is never distance culled.
                const std::vector<float> &lods = libraryModel.getScene().getNodes()[model].LodDistances;
                const float lastFactor = Max( gpConfig != nullptr ? gpConfig->getVideoSettings().GlobalLodFactor : 1.0f, 1.0f );
                if ( !lods.empty() && lods.back() > 0.0f ) {
                    distance = std::sqrt( lods.back() * 3.160494f * lastFactor );
                } else if ( lods.size() <= 1 ) {
                    distance = FLT_MAX;
                }
                Vector3f boundsMin, boundsMax;
                if ( !libraryModel.getNodeBounds( model, boundsMin, boundsMax ) ) {
                    continue;
                }
                if ( instance.Type == 3 && !instance.Bones.empty() ) {
                    // Skinned static (S list): the record's own AABB is the
                    // un-skinned rotated box, so the world box is rebuilt from
                    // the model bounds through every bone and the instance
                    // matrix - the order the shader uses (v * Bone_i * World,
                    // i.e. the bone first). All bones of a guard rail carry the
                    // same rigid matrix in the shipped data, but the union keeps
                    // a deformed palette conservative.
                    Vector3f boxMin( FLT_MAX, FLT_MAX, FLT_MAX ), boxMax( -FLT_MAX, -FLT_MAX, -FLT_MAX );
                    for ( const Matrix4x4f &bone : instance.Bones ) {
                        const Matrix4x4f world = instance.World * bone;
                        for ( int32_t corner = 0; corner < 8; corner++ ) {
                            const Vector3f point( ( corner & 1 ) ? boundsMax.x : boundsMin.x,
                                                  ( corner & 2 ) ? boundsMax.y : boundsMin.y,
                                                  ( corner & 4 ) ? boundsMax.z : boundsMin.z );
                            const Vector3f transformed = world.transformPoint( point );
                            boxMin = Vector3f( Min( boxMin.x, transformed.x ), Min( boxMin.y, transformed.y ),
                                               Min( boxMin.z, transformed.z ) );
                            boxMax = Vector3f( Max( boxMax.x, transformed.x ), Max( boxMax.y, transformed.y ),
                                               Max( boxMax.z, transformed.z ) );
                        }
                    }
                    if ( CmdLine::HasArg( "-logsinstances" ) ) {
                        // The exporter's own AABB against the one rebuilt from
                        // the geometry + palette: the two disagree if the bone
                        // composition or the instance matrix is read wrongly
                        // (a wrong order puts the rail 24 m away or upright).
                        OTDU_LOG_INFO( "SInstance: %s '%s' pos (%.2f %.2f %.2f) stored (%.2f %.2f %.2f)-(%.2f %.2f %.2f) "
                                       "built (%.2f %.2f %.2f)-(%.2f %.2f %.2f) bones %zu\n",
                                       name.c_str(), IdentifierToString( instance.Hashcode ).c_str(), instance.Position.x,
                                       instance.Position.y, instance.Position.z, instance.BoxMin.x, instance.BoxMin.y,
                                       instance.BoxMin.z, instance.BoxMax.x, instance.BoxMax.y, instance.BoxMax.z, boxMin.x,
                                       boxMin.y, boxMin.z, boxMax.x, boxMax.y, boxMax.z, instance.Bones.size() );
                    }
                    instance.BoxMin = boxMin;
                    instance.BoxMax = boxMax;
                }
            } else if ( instance.Type == 2 || instance.Type == 4 ) {
                model = getActorModel( instance.Hashcode );
                if ( model < 0 ) {
                    continue;
                }
                const Vector3f &p = instance.Position;
                instance.BoxMin = p - Vector3f( 3.0f, 3.0f, 3.0f );
                instance.BoxMax = p + Vector3f( 3.0f, 6.0f, 3.0f );
                distance = 300.0f;
            } else {
                continue;
            }
            // A/B: -instancedistmul <x> scales every instance draw distance
            // (the per-camera terms of the retail test are [ENC], taken as 1).
            static const float distanceMul = CmdLine::HasArg( "-instancedistmul" )
                                                 ? static_cast<float>( atof( CmdLine::GetArgValue( "-instancedistmul" ) ) )
                                                 : 1.0f;
            if ( distanceMul > 0.0f && distance < FLT_MAX ) {
                distance *= distanceMul;
            }
            // The per instance AnimValue states (FUN_00988d10 -> FUN_00985e40
            // per record): one blink per (blinking material, instance). The
            // draw pairs them back through InstanceBlinkStart.
            sector.InstanceBlinkStart.push_back( static_cast<uint32_t>( sector.InstanceBlinks.size() ) );
            if ( !CmdLine::HasArg( "-noworldneons" ) ) {
                const uint32_t instanceIndex = static_cast<uint32_t>( sector.Instances.size() );
                const uint32_t sectorKey = static_cast<uint32_t>( ( ( sector.Id.AreaX * 8 + sector.Id.X ) << 20 ) ^
                                                                  ( ( sector.Id.AreaY * 8 + sector.Id.Y ) << 12 ) );
                const uint8_t blinkType = instance.Type == 3 ? 1u : instance.Type;
                for ( const EdenMaterial *pMaterial : getInstanceBlinkMaterials( blinkType, model ) ) {
                    EdenNeons::Blink blink;
                    blink.initialize( sectorKey ^ ( instanceIndex * 0x9e3779b1u ) ^
                                      static_cast<uint32_t>( sector.InstanceBlinks.size() ) );
                    sector.InstanceBlinks.emplace_back( pMaterial, blink );
                }
            }
            sector.Instances.push_back( instance );
            sector.InstanceModels.push_back( model );
            sector.InstanceDistances.push_back( distance );
        }
    }
    OTDU_LOG_DEBUG( "EdenWorld: %s: %zu instances\n", name.c_str(), sector.Instances.size() );
}

bool EdenWorld::libraryNodeHasGround( int32_t node ) {
    auto it = libraryNodeGround.find( node );
    if ( it != libraryNodeGround.end() ) {
        return it->second;
    }
    bool bGround = false;
    for ( const EdenModel::Item &item : libraryModel.getItems() ) {
        if ( item.NodeIndex == node ) {
            bGround = libraryModel.getMaterials()[item.MaterialIndex].ShaderSetGUID == kGroundShaderSet;
            break;
        }
    }
    libraryNodeGround.emplace( node, bGround );
    return bGround;
}

void EdenWorld::RegisterNeons( EdenModel &model, uint32_t key,
                               std::vector<EdenMaterial *> &outSet,
                               std::vector<std::pair<EdenMaterial *, EdenNeons::Blink>> &outBlinks ) {
    if ( CmdLine::HasArg( "-noworldneons" ) ) {
        return;
    }
    for ( EdenMaterial &material : model.getMaterials() ) {
        if ( EdenNeons::IsNeonSetMaterial( material ) ) {
            outSet.push_back( &material );
        }
        if ( EdenNeons::IsBlinkMaterial( material ) ) {
            EdenNeons::Blink blink;
            blink.initialize( key ^ static_cast<uint32_t>( outBlinks.size() * 0x9e3779b1u ) );
            material.Parameters[EdenNeons::kAnimValue] = blink.Value;
            outBlinks.emplace_back( &material, blink );
        }
    }
}

const std::vector<const EdenMaterial *> &EdenWorld::getInstanceBlinkMaterials( uint8_t type, int32_t model ) {
    auto &cache = type == 1 ? libraryNodeBlinks : actorModelBlinks;
    auto it = cache.find( model );
    if ( it != cache.end() ) {
        return it->second;
    }
    std::vector<const EdenMaterial *> &list = cache[model];
    auto add = [&list]( const EdenMaterial &material ) {
        if ( EdenNeons::IsBlinkMaterial( material ) &&
             std::find( list.begin(), list.end(), &material ) == list.end() ) {
            list.push_back( &material );
        }
    };
    if ( type == 1 ) {
        // Materials drawn by the instanced node [INF: the model instance
        // material list of the original is not decoded further].
        const auto range = libraryModel.getNodeItemRange( model );
        for ( uint32_t i = range.first; i < range.first + range.second; i++ ) {
            add( libraryModel.getMaterials()[libraryModel.getItems()[i].MaterialIndex] );
        }
    } else if ( model >= 0 && model < static_cast<int32_t>( actorModels.size() ) ) {
        for ( const EdenMaterial &material : actorModels[model]->getMaterials() ) {
            add( material );
        }
    }
    return list;
}

void EdenWorld::updateNeons( const WeatherPreset *pWeather, float deltaTime ) {
    static const bool bDisabled = CmdLine::HasArg( "-noworldneons" ); // no world neon registration / update
    if ( bDisabled ) {
        return;
    }
    // FUN_009834d0: neon set (FUN_009829d0) then the AnimValue entries
    // (FUN_009821a0).
    float strength = 0.0f;
    if ( EdenNeons::GetStrength( pWeather, strength ) ) {
        for ( EdenMaterial *pMaterial : sharedNeonMaterials ) {
            EdenNeons::ApplyStrength( *pMaterial, strength );
        }
        for ( auto &pSector : sectors ) {
            for ( EdenMaterial *pMaterial : pSector->NeonMaterials ) {
                EdenNeons::ApplyStrength( *pMaterial, strength );
            }
        }
        for ( auto &pArea : areas ) {
            for ( EdenMaterial *pMaterial : pArea->NeonMaterials ) {
                EdenNeons::ApplyStrength( *pMaterial, strength );
            }
        }
    }
    for ( auto &pSector : sectors ) {
        for ( auto &entry : pSector->NeonBlinks ) {
            entry.second.update( deltaTime );
            entry.first->Parameters[EdenNeons::kAnimValue] = entry.second.Value;
        }
        for ( auto &entry : pSector->InstanceBlinks ) {
            entry.second.update( deltaTime );
        }
    }
    for ( auto &pArea : areas ) {
        for ( auto &entry : pArea->NeonBlinks ) {
            entry.second.update( deltaTime );
            entry.first->Parameters[EdenNeons::kAnimValue] = entry.second.Value;
        }
    }
}

void EdenWorld::drawInstances( EdenRenderer &renderer, const Sector &sector, const Vector3f &cameraPosition ) {
    // Frustum planes (Vulkan clip space: -w <= x, y <= w, 0 <= z <= w).
    const Matrix4x4f &vp = renderer.getViewProjection();
    // Quality profiles: the draw distance of the streamed instances relative
    // to the data's own LOD distances (100 % = retail). Read per frame so the
    // Advanced settings row applies without a reload.
    const float distanceScale = static_cast<float>( Clamp( GetGameOptions().DrawDistance, 50, 150 ) ) / 100.0f;
    Vector4f planes[6];
    for ( int i = 0; i < 6; i++ ) {
        const int axis = i / 2;
        const float sign = ( i & 1 ) ? -1.0f : 1.0f;
        float p[4];
        for ( int c = 0; c < 4; c++ ) {
            const float row = vp.m[c][axis];
            p[c] = ( axis == 2 && sign > 0.0f ) ? row : vp.m[c][3] + sign * row;
        }
        planes[i] = Vector4f( p[0], p[1], p[2], p[3] );
    }
    for ( size_t i = 0; i < sector.Instances.size(); i++ ) {
        const PmiInstance &instance = sector.Instances[i];
        const Vector3f center = ( instance.BoxMin + instance.BoxMax ) * 0.5f;
        const Vector3f extent = ( instance.BoxMax - instance.BoxMin ) * 0.5f;
        const Vector3f delta = instance.Position - cameraPosition;
        if ( instanceHideRadius > 0.0f ) {
            const float hx = instance.Position.x - instanceHideCenter.x, hz = instance.Position.z - instanceHideCenter.z;
            if ( hx * hx + hz * hz < instanceHideRadius * instanceHideRadius ) {
                continue;
            }
        }
        const float maxDistance = sector.InstanceDistances[i] * distanceScale;
        const float distanceSquared = delta.dot( delta );
        if ( distanceSquared > maxDistance * maxDistance ) {
            continue;
        }
        if ( minInstanceRatio > 0.0f &&
             extent.dot( extent ) < minInstanceRatio * minInstanceRatio * distanceSquared ) {
            continue;
        }
        bool bVisible = true;
        for ( const Vector4f &plane : planes ) {
            const float d = plane.x * center.x + plane.y * center.y + plane.z * center.z + plane.w;
            const float r = std::abs( plane.x ) * extent.x + std::abs( plane.y ) * extent.y +
                            std::abs( plane.z ) * extent.z;
            if ( d + r < 0.0f ) {
                bVisible = false;
                break;
            }
        }
        if ( !bVisible ) {
            continue;
        }
        // Occlusion cull (EdenOcclusion): hidden behind nearer geometry. The
        // cube around the bounding sphere, not the box: the far-tree impostor
        // cards are sized to the sphere and stick out of the box (a 367-pixel
        // patch of tree card went missing on the 4K drive).
        if ( renderer.isOccluded( center - Vector3f( extent.length(), extent.length(), extent.length() ),
                                  center + Vector3f( extent.length(), extent.length(), extent.length() ) ) ) {
            continue;
        }
        // Per instance AnimValue (parameter block of (material, instance)).
        // The storages are built only when used: an EdenDrawParams holds five
        // unordered_maps, which allocate even when empty, and constructing two
        // of them for every visible instance of every pass was ~22 % of the
        // main thread at 4K (-sampleprof, docs/PERF_PLAN.md 0a).
        std::optional<EdenDrawParams> blinkStorage; // this instance's blink values
        const EdenDrawParams *pBlinkParams = nullptr;
        static const bool bNoNeons = CmdLine::HasArg( "-noworldneons" );
        if ( !bNoNeons && i + 1 < sector.InstanceBlinkStart.size() &&
             sector.InstanceBlinkStart[i] != sector.InstanceBlinkStart[i + 1] ) {
            EdenDrawParams &blinkParams = blinkStorage.emplace();
            blinkParams.bDeferBlended = true;
            for ( uint32_t b = sector.InstanceBlinkStart[i]; b < sector.InstanceBlinkStart[i + 1]; b++ ) {
                blinkParams.MaterialVectors.push_back(
                    { sector.InstanceBlinks[b].first, EdenNeons::kAnimValue, sector.InstanceBlinks[b].second.Value } );
            }
            pBlinkParams = &blinkParams;
        }
        // TD_Ground instances get their sector's -s map + sectrOff
        // (night_lighting_spec 2.1; list kind 1 = O, 2 = S).
        const EdenDrawParams *pGroundParams =
            ( instance.Type == 1 || instance.Type == 3 ) && libraryNodeHasGround( sector.InstanceModels[i] )
                ? &sector.GroundInstanceParams[instance.Type == 1 ? 1 : 2]
                : nullptr;
        std::optional<EdenDrawParams> mergedStorage; // when both a blink and the ground params apply
        const EdenDrawParams *pInstanceParams = pBlinkParams;
        if ( pGroundParams != nullptr ) {
            if ( pBlinkParams == nullptr ) {
                pInstanceParams = pGroundParams;
            } else {
                EdenDrawParams &merged = mergedStorage.emplace();
                merged.Vectors = pGroundParams->Vectors;
                merged.TextureOverrides = pGroundParams->TextureOverrides;
                merged.bDeferBlended = true;
                merged.MaterialVectors = pBlinkParams->MaterialVectors;
                pInstanceParams = &merged;
            }
        }
        if ( instance.Type == 1 ) {
            // LOD selection (pmi_streaming_spec 9 #3): the INS. table's level
            // k covers up to lod[k] * 3.160494 * F, F = SET_GLOBAL_LOD_FACTOR
            // (max(F, 1) for the last level, the same rule the cull distance
            // uses). A level whose geometry did not load falls back to the
            // finest one that did.
            uint32_t lod = 0;
            const SceneNode &libraryNode = libraryModel.getScene().getNodes()[sector.InstanceModels[i]];
            const std::vector<float> &lods = libraryNode.LodDistances;
            if ( lods.size() > 1 ) {
                const float lodFactor = gpConfig != nullptr ? gpConfig->getVideoSettings().GlobalLodFactor : 1.0f;
                for ( uint32_t k = 0; k < lods.size() && k < 3; k++ ) {
                    if ( lods[k] <= 0.0f ) {
                        continue;
                    }
                    const float factor = ( k + 1 == lods.size() ) ? Max( lodFactor, 1.0f ) : lodFactor;
                    // Squared threshold, scaled the same way the cull distance
                    // is (Draw Distance 50..150 %).
                    const float threshold = lods[k] * 3.160494f * factor * distanceScale * distanceScale;
                    if ( distanceSquared <= threshold ) {
                        lod = k;
                        break;
                    }
                    lod = k + 1;
                }
                if ( lod > 2 ) {
                    lod = 2;
                }
            }
            auto range = libraryModel.getNodeItemRange( sector.InstanceModels[i], lod );
            while ( lod > 0 && range.second == 0 ) {
                lod--;
                range = libraryModel.getNodeItemRange( sector.InstanceModels[i], lod );
            }
            // The item loop of drawNodeItems with one extra filter: the lamp
            // glow faces (TD_Lamp, c913741cbe17490d) of the SF_Ligh* models
            // draw at night only (the pole / head geometry stays). -lampday
            // restores the always-on glow. glm-native, deliberately not in
            // the renderer: the cline branch carries its own bNightLights
            // there (25059ec) and the two must not collide on merge.
            static const uint64_t kLampShaderSet = 0xc913741cbe17490dull;
            const std::vector<EdenModel::Item> &items = libraryModel.getItems();
            std::vector<EdenMaterial> &materials = libraryModel.getMaterials();
            // TD_Impostor (the far tree cards): the shader has no world matrix,
            // so the old per-item draw put every card at its model-local spot
            // near the world origin - ~3,900 draws a frame that drew nothing
            // visible. They are batched in world space now (addImpostor: one
            // draw per material and pass, camera-facing, the atlas view of the
            // camera angle, lit by the weather's impostor_ambient).
            // -noimpostors skips them (the look before 2026-11-04).
            static const bool bImpostors = !CmdLine::HasArg( "-noimpostors" );
            for ( uint32_t item = range.first; item < range.first + range.second; item++ ) {
                const EdenMaterial &material = materials[items[item].MaterialIndex];
                if ( !bNightLights && material.ShaderSetGUID == kLampShaderSet ) {
                    continue;
                }
                if ( !items[item].ImpostorIndices.empty() ) {
                    // -logimpostors: the impostor card against the node's LOD0
                    // bounds (is the card the tree's size?), once per node.
                    static const bool bLogSizes = CmdLine::HasArg( "-logimpostors" );
                    static std::unordered_set<int32_t> loggedNodes;
                    if ( bLogSizes && loggedNodes.insert( sector.InstanceModels[i] ).second ) {
                        const auto lod0 = libraryModel.getNodeItemRange( sector.InstanceModels[i], 0 );
                        Vector3f lo( 1e9f, 1e9f, 1e9f ), hi( -1e9f, -1e9f, -1e9f );
                        for ( uint32_t k = lod0.first; k < lod0.first + lod0.second; k++ ) {
                            lo = Vector3f( Min( lo.x, items[k].BoundsMin.x ), Min( lo.y, items[k].BoundsMin.y ), Min( lo.z, items[k].BoundsMin.z ) );
                            hi = Vector3f( Max( hi.x, items[k].BoundsMax.x ), Max( hi.y, items[k].BoundsMax.y ), Max( hi.z, items[k].BoundsMax.z ) );
                        }
                        OTDU_LOG_INFO( "EdenWorld: impostor size '%s' card (%.1f..%.1f y, %.1f..%.1f z) LOD0 %u items (%.1f..%.1f x, %.1f..%.1f y, %.1f..%.1f z)%s",
                                       material.Name.c_str(), items[item].BoundsMin.y, items[item].BoundsMax.y,
                                       items[item].BoundsMin.z, items[item].BoundsMax.z, lod0.second, lo.x, hi.x, lo.y, hi.y,
                                       lo.z, hi.z, "\n" );
                    }
                    if ( !bImpostors || addImpostor( material, items[item], instance.World, cameraPosition ) ) {
                        continue;
                    }
                }
                if ( renderer.isProbeRecording() ) {
                    renderer.setProbeBox( instance.BoxMin, instance.BoxMax, 2 ); // -occlusionprobe
                }
                renderer.draw( items[item].Mesh, material, instance.World, pInstanceParams );
            }
        } else if ( instance.Type == 3 ) {
            libraryModel.drawNodeItemsSkinned( renderer, sector.InstanceModels[i], instance.World,
                                               instance.Bones, pInstanceParams );
        } else {
            EdenModel *pModel = actorModels[sector.InstanceModels[i]].get();
            // Direction signs (type 4, a payload): swap the composed face into
            // the model's rs_maps slot for this draw, restore right after (the
            // material is shared by every instance of the actor).
            const ActorSignInfo &signInfo = actorSignInfo[sector.InstanceModels[i]];
            GPUTexture *pFace = nullptr;
            GPUTexture *pOriginal = nullptr;
            if ( signInfo.SlotIndex >= 0 && instance.Type == 4 && instance.pPayload != nullptr && signFaces.isEnabled() ) {
                std::vector<SignTextRow> rows;
                const uint64_t payloadHash = signFaces.decodePayload( instance.pPayload, instance.PayloadSize, rows );
                if ( payloadHash != 0 ) {
                    pFace = signFaces.getFace( payloadHash, rows, signInfo.Variant, signInfo.Layout );
                }
            }
            if ( pFace != nullptr ) {
                EdenMaterial &material = pModel->getMaterials()[signInfo.MaterialIndex];
                pOriginal = material.pTextures[signInfo.SlotIndex];
                material.pTextures[signInfo.SlotIndex] = pFace;
                material.TextureHashes[signInfo.SlotIndex] = GetIdentifier64bit( pFace->Name.c_str() );
            }
            pModel->draw( renderer, instance.World, pInstanceParams );
            if ( pFace != nullptr ) {
                EdenMaterial &material = pModel->getMaterials()[signInfo.MaterialIndex];
                material.pTextures[signInfo.SlotIndex] = pOriginal;
                material.TextureHashes[signInfo.SlotIndex] =
                    pOriginal != nullptr ? GetIdentifier64bit( pOriginal->Name.c_str() ) : 0;
            }
        }
    }
}

void EdenWorld::destroy() {
    if ( gpRender == nullptr ) {
        return;
    }
    RenderDevice &device = gpRender->getDevice();
    if ( modelUnloadedCallback ) {
        for ( auto &pSector : sectors ) {
            if ( pSector->Objects.isLoaded() ) {
                modelUnloadedCallback( pSector->Objects );
            }
        }
        for ( auto &pArea : areas ) {
            if ( pArea->Objects.isLoaded() ) {
                modelUnloadedCallback( pArea->Objects );
            }
        }
    }
    for ( auto &pSector : sectors ) {
        device.destroyBuffer( pSector->Terrain.pBuffer );
        gpRender->getTextureCache().releaseOwner( pSector->BankPath.c_str() );
    }
    for ( auto &pArea : areas ) {
        device.destroyBuffer( pArea->Terrain.pBuffer );
        device.destroyBuffer( pArea->pIndexBuffer );
        gpRender->getTextureCache().releaseOwner( pArea->BankPath.c_str() );
    }
    sectors.clear();
    areas.clear();
    failedSectors.clear();
    libraryModel.destroy();
    libraryNodes.clear();
    signFaces.destroy();
    actorSignInfo.clear();
    actorModels.clear();
    actorModelIndices.clear();
    actorScenes.clear();
    actorNames.clear();
    if ( actorBank.isLoaded() ) {
        gpRender->getTextureCache().releaseOwner( actorBank.getPath().c_str() );
    }
    actorBank.unload();
    islandOcean.destroy();
    device.destroyBuffer( islandTerrain.pBuffer );
    device.destroyBuffer( pIslandIndexBuffer );
    islandTerrain = Heightfield();
    pIslandIndexBuffer = nullptr;
    numIslandIndices = 0;
    bIslandIndicesDirty = true;
    if ( islandBank.isLoaded() ) {
        gpRender->getTextureCache().releaseOwner( islandBank.getPath().c_str() );
    }
    islandBank.unload();
    pIslandColorMap = nullptr;
    bOceanVisibilityDirty = true;
    if ( weatherFxBank.isLoaded() ) {
        gpRender->getTextureCache().releaseOwner( weatherFxBank.getPath().c_str() );
    }
    weatherFxBank.unload();
    for ( GPUTexture *&pRipple : pRipples ) {
        pRipple = nullptr;
    }
    pPuddles = nullptr;
    device.destroyBuffer( pGridBuffer );
    device.destroyBuffer( pGridIndices );
    pGridBuffer = pGridIndices = nullptr;
    commonBank.unload();
}

EdenWorld::SectorId EdenWorld::getSectorAt( const Vector3f &position ) {
    SectorId id;
    const int32_t row = static_cast<int32_t>( std::floor( position.z / kSectorSize ) );
    const float offset = ( row & 1 ) ? kSectorSize * 0.5f : 0.0f;
    const int32_t column = static_cast<int32_t>( std::floor( ( position.x - offset ) / kSectorSize ) );
    id.AreaX = column >= 0 ? column / static_cast<int32_t>( kSectorsPerArea ) : -1;
    id.AreaY = row >= 0 ? row / static_cast<int32_t>( kSectorsPerArea ) : -1;
    id.X = column - id.AreaX * static_cast<int32_t>( kSectorsPerArea );
    id.Y = row - id.AreaY * static_cast<int32_t>( kSectorsPerArea );
    return id;
}

Vector3f EdenWorld::getSectorOrigin( const SectorId &id ) {
    const float x = ( id.AreaX * static_cast<float>( kSectorsPerArea ) + id.X ) * kSectorSize +
                    ( ( id.Y & 1 ) ? kSectorSize * 0.5f : 0.0f );
    const float z = ( id.AreaY * static_cast<float>( kSectorsPerArea ) + id.Y ) * kSectorSize;
    return Vector3f( x, 0.0f, z );
}

// The first direction-sign actor of the island (the -atsign spawn): scans the
// sector banks' A .pmi lists in id order for a type-4 record whose actor is an
// RS_Dir* (the text faces, sign_text.cpp). The plate faces look along the
// sign's transformed -Z; the spawn sits on that side, the car facing the plate.
bool EdenWorld::findSignSpawn( const char *pIsland, Vector3f &outPosition, float &outYaw ) {
    std::unordered_map<uint64_t, std::string> actorNames;
    const std::string actorPath = "Euro/Bnk/islands/" + std::string( pIsland ) + "/scenarics/packs/worldactors.bnk";
    Bank actorBank;
    if ( actorBank.load( actorPath.c_str() ) ) {
        const BankEntry *pEntities = actorBank.findEntry( "entities.xmb" );
        XmbDocument entities;
        if ( pEntities != nullptr && entities.load( pEntities->pData, pEntities->Size ) ) {
            XmbValue root = entities.getRoot()["ExportType_Entity"];
            for ( uint32_t i = 0; i < root.getArraySize( "ArrayOf_Entity__Actor" ); i++ ) {
                XmbValue actor = root.getArrayElement( "ArrayOf_Entity__Actor", i );
                std::string name = actor["__Name"].asString();
                if ( ToLower( name ).find( "rs_dir" ) != std::string::npos ) {
                    actorNames[actor["__Hc"].asUInt64()] = name;
                }
            }
        }
    }
    if ( actorNames.empty() ) {
        OTDU_LOG_WARN( "EdenWorld: '%s' has no direction-sign actors\n", actorPath.c_str() );
        return false;
    }

    // Area grid of the island (worldconfig.cpr, the loadWorldConfig parse).
    int32_t columns = 7, rows = 6;
    {
        std::string config;
        const std::string configPath = "Euro/Bnk/islands/" + std::string( pIsland ) + "/level/worldconfig.cpr";
        if ( LoadCPRText( configPath.c_str(), config ) ) {
            const auto readAttribute = [&config]( const char *pName, int32_t &outValue ) {
                size_t position = config.find( pName );
                if ( position == std::string::npos ) {
                    return false;
                }
                position = config.find( '"', position );
                if ( position == std::string::npos ) {
                    return false;
                }
                outValue = atoi( config.c_str() + position + 1 );
                return outValue > 0;
            };
            readAttribute( "NbColumns", columns );
            readAttribute( "NbRows", rows );
        }
    }

    for ( int32_t ay = 0; ay < rows; ay++ ) {
        for ( int32_t ax = 0; ax < columns; ax++ ) {
            for ( int32_t sy = 0; sy < static_cast<int32_t>( kSectorsPerArea ); sy++ ) {
                for ( int32_t sx = 0; sx < static_cast<int32_t>( kSectorsPerArea ); sx++ ) {
                    const SectorId id{ ax, ay, sx, sy };
                    const std::string path = GetSectorBankPath( pIsland, id );
                    if ( gpFile == nullptr || !gpFile->fileExists( path.c_str() ) ) {
                        continue;
                    }
                    Bank bank;
                    if ( !bank.load( path.c_str() ) ) {
                        continue;
                    }
                    const BankEntry *pEntry = bank.findEntry( ( SectorName( id ) + "-a" ).c_str(), "pmi" );
                    if ( pEntry == nullptr ) {
                        continue;
                    }
                    std::vector<PmiInstance> instances;
                    if ( !ParsePmi( pEntry->pData, pEntry->Size, getSectorOrigin( id ), instances ) ) {
                        continue;
                    }
                    for ( const PmiInstance &instance : instances ) {
                        if ( instance.Type != 4 ) {
                            continue;
                        }
                        const auto nameIt = actorNames.find( instance.Hashcode );
                        if ( nameIt == actorNames.end() ) {
                            continue;
                        }
                        Vector3f forward( instance.World.m[0][2], instance.World.m[1][2], instance.World.m[2][2] );
                        forward.y = 0.0f;
                        const float length = std::sqrt( forward.x * forward.x + forward.z * forward.z );
                        if ( length < 1e-5f ) {
                            continue;
                        }
                        forward.x /= length;
                        forward.z /= length;
                        outPosition = instance.Position;
                        outYaw = std::atan2( forward.x, forward.z );
                        OTDU_LOG_INFO( "EdenWorld: -atsign (%.1f, %.1f, %.1f) '%s'\n", outPosition.x, outPosition.y,
                                       outPosition.z, nameIt->second.c_str() );
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool EdenWorld::isSectorLoaded( const SectorId &id ) const {
    for ( const auto &pSector : sectors ) {
        if ( pSector->Id == id ) {
            return true;
        }
    }
    return false;
}

bool EdenWorld::isSectorShown( const SectorId &id ) const {
    for ( const auto &pSector : sectors ) {
        if ( pSector->Id == id ) {
            return !pSector->bPreloaded;
        }
    }
    return false;
}

bool EdenWorld::loadHeightfield( const Bank &bank, const char *pBaseName, const char *pTextureOwner,
                                 Heightfield &out ) {
    const BankEntry *pScene = bank.findEntry( pBaseName, "3dd" );
    const BankEntry *pGeometry = bank.findEntry( pBaseName, "3dg" );
    const BankEntry *pMaterials = bank.findEntry( pBaseName, "2dm" );
    if ( pScene == nullptr || pGeometry == nullptr || pMaterials == nullptr ) {
        return false;
    }
    Render3DD scene;
    Render3DG geometry;
    Render2DM materials;
    if ( !scene.parse( pScene->pData, pScene->Size, false ) || !geometry.parse( pGeometry->pData, pGeometry->Size, false ) ||
         !materials.parse( pMaterials->pData, pMaterials->Size ) ) {
        return false;
    }
    const GeometryPrimitive *pPrimitive = nullptr;
    uint64_t materialHash = 0;
    for ( const SceneNode &node : scene.getNodes() ) {
        const Geometry *pGeometryData = geometry.find( node.ObjectHashcode );
        if ( pGeometryData == nullptr ) {
            continue;
        }
        for ( const GeometryPrimitive &primitive : pGeometryData->Primitives ) {
            if ( primitive.NumRawVertex == kHeightfieldSize * kHeightfieldSize ) {
                pPrimitive = &primitive;
                materialHash = primitive.MaterialIndex < node.Materials.size() ? node.Materials[primitive.MaterialIndex]
                                                                               : 0;
                break;
            }
        }
        if ( pPrimitive != nullptr ) {
            break;
        }
    }
    const MaterialDesc *pDesc = materials.find( materialHash );
    if ( pDesc == nullptr && !materials.getMaterials().empty() ) {
        pDesc = &materials.getMaterials().begin()->second;
    }
    if ( pPrimitive == nullptr || pDesc == nullptr ) {
        OTDU_LOG_WARN( "EdenWorld: no heightfield in '%s'\n", pBaseName );
        return false;
    }

    // Heights (stream 0, R32)
    out.Heights.resize( kHeightfieldSize * kHeightfieldSize );
    if ( pPrimitive->RawStrides[0] == 4 ) {
        memcpy( out.Heights.data(), pPrimitive->RawStreams[0].data(), out.Heights.size() * sizeof( float ) );
    }

    if ( CmdLine::HasArg( "-dumpheights" ) ) {
        // 257 x 257 f32 heights + the raw streams, for offline comparison of the area / sector layers
        const std::string dumpName = std::string( "_heights_" ) + pBaseName;
        if ( FILE *pFile = fopen( ( dumpName + ".bin" ).c_str(), "wb" ) ) {
            fwrite( out.Heights.data(), sizeof( float ), out.Heights.size(), pFile );
            fclose( pFile );
        }
        if ( FILE *pFile = fopen( ( dumpName + ".s1.bin" ).c_str(), "wb" ) ) {
            fwrite( pPrimitive->RawStreams[1].data(), 1, pPrimitive->RawStreams[1].size(), pFile );
            fclose( pFile );
        }
        OTDU_LOG_INFO( "dumpheights: %s strides %u/%u stream sizes %zu/%zu\n", pBaseName, pPrimitive->RawStrides[0],
                       pPrimitive->RawStrides[1], pPrimitive->RawStreams[0].size(), pPrimitive->RawStreams[1].size() );
    }

    std::vector<uint8_t> data;
    data.insert( data.end(), pPrimitive->RawStreams[0].begin(), pPrimitive->RawStreams[0].end() );
    const size_t stream1Offset = Align<size_t>( data.size(), 16 );
    data.resize( stream1Offset );
    data.insert( data.end(), pPrimitive->RawStreams[1].begin(), pPrimitive->RawStreams[1].end() );
    out.pBuffer = gpRender->getDevice().createBuffer( data.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, data.data() );

    EdenMesh &mesh = out.Mesh;
    mesh.NumStreams = 3;
    mesh.pStreamBuffers[0] = out.pBuffer;
    mesh.pStreamBuffers[1] = out.pBuffer;
    mesh.pStreamBuffers[2] = pGridBuffer;
    mesh.StreamOffsets[0] = 0;
    mesh.StreamOffsets[1] = stream1Offset;
    mesh.StreamOffsets[2] = 0;
    mesh.Strides[0] = pPrimitive->RawStrides[0];
    mesh.Strides[1] = pPrimitive->RawStrides[1];
    mesh.Strides[2] = 8;
    mesh.Attributes = pPrimitive->RawAttributes;
    RawVertexAttribute gridAttribute;
    gridAttribute.Usage = 5; // TEXCOORD0
    gridAttribute.UsageIndex = 0;
    gridAttribute.Stream = 2;
    gridAttribute.Format = VAF_TEXCOORD_R32G32_SFLOAT;
    gridAttribute.Offset = 0;
    mesh.Attributes.push_back( gridAttribute );
    mesh.pIndexBuffer = pGridIndices;
    mesh.FirstIndex = 0;
    mesh.IndexCount = numGridIndices;
    mesh.computeLayoutKey();

    out.Materials.resize( 1 );
    pRenderer->setupMaterial( *pDesc, out.Materials[0], pTextureOwner );
    // The -s map on the SHADOW slot (night_lighting_spec 2: TD_HeightMap
    // samples it as g_Shadow_Snight_Grass_Sampler - R sun shadow, G the
    // baked street-lamp light drawn at night through UserVector1.y, B the
    // grass mask). setupMaterial binds it from the 2DM hash when the bank
    // texture resolved; assign it explicitly when it did not (the island
    // terrain has no -s map: find returns null there).
    if ( GPUTexture *pSectorShadow =
             gpRender->getTextureCache().find( pTextureOwner, ( std::string( pBaseName ) + "-s" ).c_str() ) ) {
        if ( pRenderer->assignTexture( out.Materials[0], "SHADOW", pSectorShadow ) ) {
            pRenderer->updateProgram( out.Materials[0] );
        }
    }
    static const bool bDebugGrid = CmdLine::HasArg( "-hmdebuggrid" );
    if ( CmdLine::HasArg( "-hmdebugps" ) ) {
        out.Materials[0].pProgram = pRenderer->getShaderSystem().getProgram(
            "TD_HeightMap", CmdLine::GetArgInt( "-hmvs", 0 ), CmdLine::GetArgInt( "-hmdebugps", 56 ) );
        return true;
    }
    if ( StringNCmpI( pBaseName, "area", 4 ) == 0 || bDebugGrid ) {
        out.Materials[0].HeightfieldGrid = 1;
        pRenderer->updateProgram( out.Materials[0] );
    }
    if ( !CmdLine::HasArg( "-terrainblend" ) ) {
        // The terrain material is alpha blended (5/6) without depth write and was sorted per
        // mesh by its origin corner, so a 6 km area mesh painted over the 768 m sectors (the
        // see-through / floating sheets): draw it opaque with depth write. Owner-verified on
        // Ibiza, Hawaii and Casino. -terrainblend restores the old behaviour for A/B.
        out.Materials[0].SrcBlend = 2;
        out.Materials[0].DstBlend = 1;
        out.Materials[0].bDepthWrite = true;
        out.Materials[0].bDepthTest = true;
    }
    out.MaterialIndex = 0;
    return out.Materials[0].pProgram != nullptr;
}

bool EdenWorld::loadSector( const SectorId &id ) {
    if ( id.AreaX < 0 || id.AreaY < 0 || id.AreaX >= areaColumns || id.AreaY >= areaRows || id.X < 0 || id.Y < 0 ||
         id.X >= static_cast<int32_t>( kSectorsPerArea ) || id.Y >= static_cast<int32_t>( kSectorsPerArea ) ) {
        return false;
    }
    auto pSector = std::make_unique<Sector>();
    pSector->Id = id;
    pSector->Origin = getSectorOrigin( id );
    // -logsectortimes: the ms of each stage of the load (the streaming hitch).
    static const bool bLogTimes = CmdLine::HasArg( "-logsectortimes" );
    const uint64_t frequency = SDL_GetPerformanceFrequency();
    uint64_t stageStart = SDL_GetPerformanceCounter();
    std::string stageTimes;
    auto stage = [&]( const char *pName ) {
        if ( !bLogTimes ) {
            return;
        }
        const uint64_t now = SDL_GetPerformanceCounter();
        char piece[48];
        snprintf( piece, sizeof( piece ), " %s %.1f", pName,
                  static_cast<double>( now - stageStart ) * 1000.0 / static_cast<double>( frequency ) );
        stageTimes += piece;
        stageStart = now;
    };
    const std::string bankPath = getSectorBankPath( id );
    const char *path = bankPath.c_str();
    pSector->BankPath = bankPath;
    if ( !gpFile->fileExists( path ) ) {
        OTDU_LOG_WARN( "EdenWorld: '%s' not found, sector skipped\n", path );
        return false;
    }
    if ( !pSector->SectorBank.load( path ) ) {
        OTDU_LOG_WARN( "EdenWorld: '%s' could not be loaded, skipped\n", path );
        return false;
    }
    stage( "bank" );
    gpRender->getTextureCache().loadBankTextures( pSector->SectorBank, path );
    stage( "textures" );
    const std::string name = SectorName( id );
    loadHeightfield( pSector->SectorBank, name.c_str(), path, pSector->Terrain );
    stage( "heightfield" );
    // Sector shadow map (-s.2db) on the SHADOW slot of the objects.
    // FUN_00982af0 (end of the sector setup FUN_00986760) [CODE]: for the
    // sector materials of TD_Road (every SHADOW slot) and TD_Ground /
    // TD_Ground_HM (slot 2): texture = the -s map, addressing 3 / 3 (CLAMP),
    // and for the two TD_Ground families sectrOff = (sector x, 0, sector z, 0)
    // (night_lighting_spec 2.1). The TD_Ground vs samples the -s map at
    // (world.xz - sectrOff.xz) / 768: with the 2DM value (0, 0, 0, 768) the
    // lookup was off by half a sector on the odd sector rows (x origin +
    // 384 m), so the baked lamp pools landed in the wrong place. The -s map
    // G channel is the baked street-lamp light, drawn at night by
    // UserVector1.y (outside_street_lightmap) from the weather.
    GPUTexture *pShadowMap = gpRender->getTextureCache().find( path, ( name + "-s" ).c_str() );
    EdenRenderer *pEdenRenderer = pRenderer;
    const Vector3f sectorOrigin = pSector->Origin;
    auto materialSetup = [pShadowMap, pEdenRenderer, sectorOrigin]( EdenMaterial &material ) {
        if ( pShadowMap == nullptr ) {
            return;
        }
        const bool bGround = material.ShaderSetGUID == kGroundShaderSet || material.ShaderSetGUID == kGroundHMShaderSet;
        if ( !bGround && material.ShaderSetGUID != kRoadShaderSet ) {
            if ( pEdenRenderer->assignTexture( material, "SHADOW", pShadowMap ) ) {
                pEdenRenderer->updateProgram( material );
            }
            return;
        }
        // TD_Ground / TD_Ground_HM: slot 2 whatever its 2DM type (it is
        // SHADOW in the library and sector TD_Ground materials but
        // 5067tunnel_mesh, whose slot 2 is DETAIL1 [DATA]).
        bool bAssigned = false;
        for ( uint32_t i = 0; i < material.NumSlots; i++ ) {
            if ( bGround ? i == 2 : material.SlotTypes[i] == kShadowSlotType ) {
                material.pTextures[i] = pShadowMap;
                material.TextureHashes[i] = GetIdentifier64bit( pShadowMap->Name.c_str() );
                material.SamplerIndex[i] = kSamplerLinearClampUV;
                bAssigned = true;
            }
        }
        if ( bGround ) {
            material.Parameters[kSectorOffset] = Vector4f( sectorOrigin.x, 0.0f, sectorOrigin.z, 0.0f );
        }
        if ( bAssigned ) {
            pEdenRenderer->updateProgram( material );
        }
    };
    pSector->Objects.setKeepCollision( true );
    pSector->Objects.load( *pRenderer, pSector->SectorBank, ( name + "-o" ).c_str(), &libraryMaterials, path, nullptr,
                           materialSetup );
    stage( "objects" );
    if ( modelLoadedCallback && pSector->Objects.isLoaded() ) {
        modelLoadedCallback( pSector->Objects );
    }
    // The heightfield cells inside tunnel / bridge volumes (-nocarve for the
    // A/B) are marked now that the node bounds exist; the index split happens
    // at the first draw. The carve ground (the nodes' walkable triangles for
    // the drive physics) is indexed with them.
    if ( !CmdLine::HasArg( "-nocarve" ) ) {
        MarkCarvedCells( *pSector );
        BuildCarveGround( *pSector );
        // Only the cells the structure actually floors over may lose their
        // terrain: the rest are holes you can see through and fall through.
        // -noprunecarve restores the holes for the A/B.
        if ( !CmdLine::HasArg( "-noprunecarve" ) ) {
            PruneUncoveredCarveCells( *pSector );
        }
    }
    stage( "carve" );
    if ( CmdLine::HasArg( "-logtunnels" ) && pSector->Objects.isLoaded() ) {
        // World bounds of the sector nodes named like a tunnel (finding the
        // tunnels of an island: their meshes are "<sector>Tunnel_mesh" /
        // "<sector>Shadow_tunn" [DATA]).
        const std::vector<SceneNode> &nodes = pSector->Objects.getScene().getNodes();
        for ( int32_t i = 0; i < static_cast<int32_t>( nodes.size() ); i++ ) {
            std::string lower = nodes[i].Name;
            std::transform( lower.begin(), lower.end(), lower.begin(), ::tolower );
            Vector3f boundsMin, boundsMax;
            if ( CmdLine::HasArg( "-logallnodes" ) ) {
                OTDU_LOG_INFO( "Node: %s #%d '%s' obj %016llx\n", name.c_str(), i, nodes[i].Name.c_str(),
                               static_cast<unsigned long long>( nodes[i].ObjectHashcode ) );
            }
            const int32_t parent = nodes[i].Parent;
            std::string parentName = parent >= 0 ? nodes[parent].Name : std::string();
            std::transform( parentName.begin(), parentName.end(), parentName.begin(), ::tolower );
            if ( ( lower.find( "tun" ) != std::string::npos || parentName.find( "tunn" ) != std::string::npos ) &&
                 pSector->Objects.getNodeBounds( i, boundsMin, boundsMax ) ) {
                OTDU_LOG_INFO( "Tunnel: %s '%s' x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f\n", name.c_str(), nodes[i].Name.c_str(),
                               boundsMin.x + pSector->Origin.x, boundsMax.x + pSector->Origin.x, boundsMin.y, boundsMax.y,
                               boundsMin.z + pSector->Origin.z, boundsMax.z + pSector->Origin.z );
            }
        }
    }
    // Shore foam (TD_backwash, material "Ecume", sector-5-0-6-7 / 5-1-6-1
    // only on Ibiza [DATA]):
    // - its texture coordinate animation: ecume_0.uva in the same bank, id
    //   0x049c2e1c84816bae = the UVA. entry of the -o.2dm UVAA section
    //   [DATA]; 8 s loop, V offset -2 -> -0.25 -> -2, V scale 3 -> 1.2 -> 3
    //   (the surf runs up the strip and back). TD_backwash vs: TC0.xy =
    //   (u, v, 1) . __UVA_0 rows 0 / 1 (dp3 with c71.xyw / c72.xyw) [SHADER].
    //   Without the binding __UVA_0 is the identity: static foam.
    // - its COLOR slot (plage.2db) addressing: 2DM slot bytes +0x18/+0x19 =
    //   1 / 2 (U / V) where every other slot has equal values [DATA]; with
    //   the D3DTADDRESS numbering (1 WRAP, 2 MIRROR, 3 CLAMP: the ocean code
    //   writes 1 for the world-tiled NoiseNMap and 3 for the projected
    //   reflection [CODE FUN_008be1e0]) V is MIRROR [INF]. The strip has v in
    //   {0, 1} and u*6 tiles along the shore (u restarts at 0 on duplicated
    //   seam vertices) [DATA sector-5-0-6-7-o.3dg], so a mirror sampler on
    //   both axes is seamless along u and gives the mirrored V of the
    //   original (wrap leaves a hard opaque / transparent seam at v' = 0, -1:
    //   plage.2db R (= output alpha) is 255 at v = 0 and 0 at v = 1 [DATA]).
    const uint32_t numUVAnimations = pSector->Objects.bindUVAnimations( pSector->SectorBank );
    for ( EdenMaterial &material : pSector->Objects.getMaterials() ) {
        if ( material.ShaderSetGUID == kBackwashShaderSet ) {
            material.SamplerIndex[0] = kSamplerLinearMirror;
        }
    }
    // OpenTDU2 shore swell: the beach gradient under the foam strip. The strip
    // has a fixed footprint; translating it vertically only floats it. Shifted
    // along the gradient by (sea level / slope) it walks up and down the sand
    // with the tide, the surf zone the owner asked to see move.
    for ( const EdenModel::Item &item : pSector->Objects.getItems() ) {
        if ( pSector->Objects.getMaterials()[item.MaterialIndex].ShaderSetGUID != kBackwashShaderSet ) {
            continue;
        }
        const std::vector<float> &heights = pSector->Terrain.Heights;
        if ( heights.empty() ) {
            break;
        }
        const Vector3f center = pSector->Objects.getNodeTransform( item.NodeIndex )
                                    .transformPoint( ( item.BoundsMin + item.BoundsMax ) * 0.5f );
        auto sampleHeight = [&heights]( float x, float z ) {
            const float gx = Clamp( x / 3.0f, 0.0f, 255.999f );
            const float gz = Clamp( z / 3.0f, 0.0f, 255.999f );
            const uint32_t ix = static_cast<uint32_t>( gx ), iz = static_cast<uint32_t>( gz );
            const float fx = gx - ix, fz = gz - iz;
            const float h00 = heights[iz * kHeightfieldSize + ix];
            const float h10 = heights[iz * kHeightfieldSize + ix + 1];
            const float h01 = heights[( iz + 1 ) * kHeightfieldSize + ix];
            const float h11 = heights[( iz + 1 ) * kHeightfieldSize + ix + 1];
            return ( h00 * ( 1 - fx ) + h10 * fx ) * ( 1 - fz ) + ( h01 * ( 1 - fx ) + h11 * fx ) * fz;
        };
        constexpr float kStep = 96.0f; // coarse: the foam sits over the sea bed, the shore only shows at this scale
        const float gradientX =
            ( sampleHeight( center.x + kStep, center.z ) - sampleHeight( center.x - kStep, center.z ) ) /
            ( 2.0f * kStep );
        const float gradientZ =
            ( sampleHeight( center.x, center.z + kStep ) - sampleHeight( center.x, center.z - kStep ) ) /
            ( 2.0f * kStep );
        const float lengthSquared = gradientX * gradientX + gradientZ * gradientZ;
        constexpr float kMinSlope = 0.025f; // 1:40, beyond this the run-up is clamped
        const float scale = 1.0f / Max( lengthSquared, kMinSlope * kMinSlope );
        pSector->FoamTideVector = Vector3f( gradientX * scale, 0.0f, gradientZ * scale );
        if ( getenv( "OTDU_OCEAN_LOG" ) != nullptr ) {
            OTDU_LOG_INFO( "EdenWorld: %s: foam '%s' centre (%.1f, %.1f), beach gradient (%.3f, %.3f), run-up %.1f m "
                           "per m of sea level\n",
                           name.c_str(), pSector->Objects.getMaterials()[item.MaterialIndex].Name.c_str(), center.x,
                           center.z, gradientX, gradientZ, pSector->FoamTideVector.length() );
        }
        break;
    }
    if ( numUVAnimations != 0 && getenv( "OTDU_OCEAN_LOG" ) != nullptr ) {
        OTDU_LOG_INFO( "EdenWorld: %s: %u UV animations\n", name.c_str(), numUVAnimations );
    }
    // FUN_00986e90: streamed world object loaded -> FUN_00985e40 for its
    // model instance [CODE]; the sector -o scene is taken as one model
    // instance [INF].
    RegisterNeons( pSector->Objects, static_cast<uint32_t>( GetIdentifier64bit( name.c_str() ) ),
                   pSector->NeonMaterials, pSector->NeonBlinks );
    // TD_Ground parameters of the O / S list instances (night_lighting_spec
    // 2.1). FUN_00982af0 fills the override record { TD_Ground guid, SHADOW,
    // texture = the -s map, addressing 2 / 2 } of the O and S lists and
    // FUN_00987ec0 writes the instance sectrOff = (sector x, 0, sector z,
    // 768.0): the TD_Ground vs samples the -s map at (world.xz - sectrOff.xz)
    // / 768, i.e. the sector's own lamp pools at the instance's place. The V
    // list has no TD_Ground models in the shipped data (its record is not
    // even written by readable retail code).
    for ( uint32_t list = 0; list < 3; list++ ) {
        EdenDrawParams &params = pSector->GroundInstanceParams[list];
        params.bDeferBlended = true;
        params.Vectors["sectrOff"] = Vector4f( pSector->Origin.x, 0.0f, pSector->Origin.z, 768.0f );
        if ( list != 0 && pShadowMap != nullptr ) {
            EdenDrawParams::TextureOverride entry;
            entry.ShaderSetGUID = kGroundShaderSet;
            entry.SlotType = kShadowSlotType;
            entry.pTexture = pShadowMap;
            entry.Sampler = kSamplerLinearMirror;
            params.TextureOverrides.push_back( entry );
        }
    }
    stage( "misc" );
    loadInstances( *pSector, name );
    // The .pgr grass grid payload (grass_spec.md 2) - kept compressed, the
    // rows decode on the fly for the cells around the camera.
    if ( GrassEnabled() ) {
        if ( const BankEntry *pPgr = pSector->SectorBank.findEntry( name.c_str(), "pgr" ) ) {
            pSector->Pgr.assign( pPgr->pData, pPgr->pData + pPgr->Size );
        }
    }
    stage( "instances" );
    if ( bLogTimes ) {
        OTDU_LOG_INFO( "[sector] %s ms:%s\n", name.c_str(), stageTimes.c_str() );
    }
    OTDU_LOG_DEBUG( "EdenWorld: %s at (%.0f, %.0f) objects (%.0f %.0f %.0f)-(%.0f %.0f %.0f)\n", name.c_str(),
                    pSector->Origin.x, pSector->Origin.z, pSector->Objects.getBoundsMin().x,
                    pSector->Objects.getBoundsMin().y, pSector->Objects.getBoundsMin().z,
                    pSector->Objects.getBoundsMax().x, pSector->Objects.getBoundsMax().y,
                    pSector->Objects.getBoundsMax().z );
    sectors.push_back( std::move( pSector ) );
    markAreasDirty( id );
    bOceanVisibilityDirty = true;
    return true;
}

bool EdenWorld::loadArea( int32_t x, int32_t y ) {
    auto pArea = std::make_unique<Area>();
    pArea->X = x;
    pArea->Y = y;
    pArea->Origin = Vector3f( x * kSectorsPerArea * kSectorSize, 0.0f, y * kSectorsPerArea * kSectorSize );
    char path[256];
    snprintf( path, sizeof( path ), "%sarea-%d/area-%d-%d/area-%d-%d.bnk", islandFolder.c_str(), x, x, y, x, y );
    pArea->BankPath = path;
    if ( !gpFile->fileExists( path ) ) {
        return false; // open sea or data not installed (counted by loadAround)
    }
    if ( !pArea->AreaBank.load( path ) ) {
        return false;
    }
    gpRender->getTextureCache().loadBankTextures( pArea->AreaBank, path );
    char name[64];
    snprintf( name, sizeof( name ), "area-%d-%d", x, y );
    loadHeightfield( pArea->AreaBank, name, path, pArea->Terrain );
    pArea->Terrain.Spacing = 24.0f;
    pArea->Objects.load( *pRenderer, pArea->AreaBank, ( std::string( name ) + "-o" ).c_str(), &libraryMaterials, path );
    pArea->Objects.bindUVAnimations( pArea->AreaBank );
    if ( modelLoadedCallback && pArea->Objects.isLoaded() ) {
        modelLoadedCallback( pArea->Objects );
    }
    // Sector cell groups of the area objects (FUN_00992890 [CODE]): the area
    // -o scene hangs each 768 m ocean tile (and the far objects) under a dummy
    // node "Sector-ax-ay-sx-sy" [DATA area-x-y-o.3dd: BLOCK -> SECTOR-... ->
    // OCEAN tile]. On odd rows the first cell also looks up the node of the
    // previous area's column 7, whose staggered cell overlaps this area by
    // half a sector. The groups let updateOceanVisibility hide an area tile
    // exactly when its own sector tile is loaded - hiding the whole area or
    // guessing by centre left sea holes that popped as the sector ring moved.
    if ( pArea->Objects.isLoaded() ) {
        const Render3DD &scene = pArea->Objects.getScene();
        std::vector<std::pair<int32_t, SectorId>> groups;
        for ( int32_t sy = 0; sy < static_cast<int32_t>( kSectorsPerArea ); sy++ ) {
            for ( int32_t sx = 0; sx < static_cast<int32_t>( kSectorsPerArea ); sx++ ) {
                if ( sx == 0 && ( sy & 1 ) != 0 ) {
                    const SectorId previous = { x - 1, y, static_cast<int32_t>( kSectorsPerArea ) - 1, sy };
                    const int32_t node = scene.findNode( SectorName( previous ).c_str() );
                    if ( node >= 0 ) {
                        groups.emplace_back( node, previous );
                    }
                }
                const SectorId id = { x, y, sx, sy };
                const int32_t node = scene.findNode( SectorName( id ).c_str() );
                if ( node >= 0 ) {
                    groups.emplace_back( node, id );
                }
            }
        }
        for ( const EdenModel::Item &item : pArea->Objects.getItems() ) {
            SectorId sector = { INT32_MIN, 0, 0, 0 };
            for ( const auto &group : groups ) {
                if ( scene.isDescendantOf( item.NodeIndex, group.first ) ) {
                    sector = group.second;
                    break;
                }
            }
            pArea->ItemSectors.push_back( sector );
        }
        if ( getenv( "OTDU_OCEAN_LOG" ) != nullptr ) {
            OTDU_LOG_INFO( "EdenWorld: %s: %zu items, %zu sector groups\n", name, pArea->Objects.getItems().size(),
                           groups.size() );
        }
    }    RegisterNeons( pArea->Objects, static_cast<uint32_t>( GetIdentifier64bit( name ) ), pArea->NeonMaterials,
                   pArea->NeonBlinks );    areas.push_back( std::move( pArea ) );
    bIslandIndicesDirty = true;
    bOceanVisibilityDirty = true;
    return true;
}

void EdenWorld::markAreasDirty( const SectorId &id ) {
    // A sector of an odd row overlaps the next area by half a sector.
    for ( auto &pArea : areas ) {
        if ( pArea->Y == id.AreaY && ( pArea->X == id.AreaX || pArea->X == id.AreaX + 1 ) ) {
            pArea->bIndicesDirty = true;
        }
    }
}

uint32_t EdenWorld::unloadFar( const Vector3f &position, int32_t sectorRadius, int32_t maxSectors ) {
    return unloadFar( position, position, sectorRadius, maxSectors );
}

uint32_t EdenWorld::unloadFar( const Vector3f &position, const Vector3f &ahead, int32_t sectorRadius,
                               int32_t maxSectors ) {
    const SectorId center = getSectorAt( position );
    const int32_t centerColumn = center.AreaX * kSectorsPerArea + center.X;
    const int32_t centerRow = center.AreaY * kSectorsPerArea + center.Y;
    const SectorId aheadCenter = getSectorAt( ahead );
    const int32_t aheadColumn = aheadCenter.AreaX * kSectorsPerArea + aheadCenter.X;
    const int32_t aheadRow = aheadCenter.AreaY * kSectorsPerArea + aheadCenter.Y;
    uint32_t numUnloaded = 0;
    RenderDevice &device = gpRender->getDevice();
    for ( auto it = sectors.begin(); it != sectors.end(); ) {
        const SectorId &id = ( *it )->Id;
        const int32_t column = id.AreaX * kSectorsPerArea + id.X;
        const int32_t row = id.AreaY * kSectorsPerArea + id.Y;
        if ( ( std::abs( column - centerColumn ) <= sectorRadius && std::abs( row - centerRow ) <= sectorRadius ) ||
             ( std::abs( column - aheadColumn ) <= sectorRadius && std::abs( row - aheadRow ) <= sectorRadius ) ) {
            ++it;
            continue;
        }
        const SectorId unloadedId = id;
        if ( modelUnloadedCallback && ( *it )->Objects.isLoaded() ) {
            modelUnloadedCallback( ( *it )->Objects );
        }
        device.destroyBuffer( ( *it )->Terrain.pBuffer );
        destroyCarveBuffers( **it );
        gpRender->getTextureCache().releaseOwner( ( *it )->BankPath.c_str() );
        it = sectors.erase( it );
        markAreasDirty( unloadedId );
        numUnloaded++;
        if ( maxSectors > 0 && numUnloaded >= static_cast<uint32_t>( maxSectors ) ) {
            // The caller's budget is spent: the rest goes on the following
            // frames' calls (the world map pans call this every frame).
            break;
        }
    }
    if ( numUnloaded != 0 ) {
        bOceanVisibilityDirty = true;
        drawEpoch++;
        OTDU_LOG_INFO( "EdenWorld: %u sectors unloaded, %zu loaded\n", numUnloaded, sectors.size() );
    }
    return numUnloaded;
}

void EdenWorld::scanTunnelTerrain() {
    // Node names are French-authored data: 'tun' covers the TUNNEL / *_W_T
    // pairs of the session-42 tunnel; pont / viad (and the English words)
    // are the bridge spellings to watch for.
    static const char *kKeywords[] = { "tun", "pont", "viad", "bridge" };
    OTDU_LOG_INFO( "scantunnels: %d x %d areas, every sector bank one at a time\n", areaColumns, areaRows );
    std::unordered_set<int32_t> keep;
    keep.reserve( sectors.size() * 2 );
    for ( const auto &pSector : sectors ) {
        keep.insert( SectorRowColumn( pSector->Id ) );
    }
    const int32_t maxColumn = areaColumns * static_cast<int32_t>( kSectorsPerArea );
    const int32_t maxRow = areaRows * static_cast<int32_t>( kSectorsPerArea );
    uint32_t numScanned = 0, numMissing = 0, numNodes = 0, numHits = 0;
    uint32_t numCarvedTotal = 0, numUncoveredTotal = 0;
    for ( int32_t row = 0; row < maxRow; row++ ) {
        for ( int32_t column = 0; column < maxColumn; column++ ) {
            SectorId id;
            id.AreaX = column / static_cast<int32_t>( kSectorsPerArea );
            id.AreaY = row / static_cast<int32_t>( kSectorsPerArea );
            id.X = column % static_cast<int32_t>( kSectorsPerArea );
            id.Y = row % static_cast<int32_t>( kSectorsPerArea );
            Sector *pTarget = nullptr;
            for ( auto &pSector : sectors ) {
                if ( pSector->Id == id ) {
                    pTarget = pSector.get();
                    break;
                }
            }
            if ( pTarget == nullptr && !loadSector( id ) ) {
                numMissing++;
                continue;
            }
            if ( pTarget == nullptr ) {
                pTarget = sectors.back().get();
            }
            Sector &sector = *pTarget;
            numScanned++;
            // The survey blocks a frame for minutes: beat the watchdog's kill
            // guard per sector (source/core/watchdog.h), else the default
            // kill threshold ends the run mid-scan.
            Watchdog::Beat( "scantunnels", numScanned );
            const std::vector<SceneNode> &nodes = sector.Objects.getScene().getNodes();
            const std::vector<float> &heights = sector.Terrain.Heights;
            const float spacing = sector.Terrain.Spacing;
            for ( int32_t i = 0; i < static_cast<int32_t>( nodes.size() ); i++ ) {
                Vector3f boundsMin, boundsMax;
                if ( !sector.Objects.getNodeBounds( i, boundsMin, boundsMax ) ) {
                    continue;
                }
                const int32_t parent = nodes[i].Parent;
                std::string lower = nodes[i].Name;
                std::transform( lower.begin(), lower.end(), lower.begin(), ::tolower );
                std::string parentName = parent >= 0 ? nodes[parent].Name : std::string();
                std::transform( parentName.begin(), parentName.end(), parentName.begin(), ::tolower );
                const char *kind = nullptr;
                for ( const char *keyword : kKeywords ) {
                    if ( lower.find( keyword ) != std::string::npos || parentName.find( keyword ) != std::string::npos ) {
                        kind = keyword;
                        break;
                    }
                }
                if ( kind == nullptr ) {
                    continue;
                }
                numNodes++;
                if ( heights.empty() ) {
                    OTDU_LOG_INFO( "scantunnels: ok   %s '%s' (kw %s) x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f (no heightfield)\n",
                                   SectorName( id ).c_str(), nodes[i].Name.c_str(), kind, boundsMin.x + sector.Origin.x,
                                   boundsMax.x + sector.Origin.x, boundsMin.y, boundsMax.y, boundsMin.z + sector.Origin.z,
                                   boundsMax.z + sector.Origin.z );
                    continue;
                }
                // Sample the heightfield inside the footprint at >= 3 m
                // steps (~4k samples cap on the longest nodes).
                const float x0 = boundsMin.x + sector.Origin.x, x1 = boundsMax.x + sector.Origin.x;
                const float z0 = boundsMin.z + sector.Origin.z, z1 = boundsMax.z + sector.Origin.z;
                const float area = Max( 0.0f, x1 - x0 ) * Max( 0.0f, z1 - z0 );
                const float step = Max( spacing, std::sqrt( area / 4096.0f ) );
                uint32_t numSamples = 0, numBand = 0, numBelow = 0, numAbove = 0;
                float heightMin = FLT_MAX, heightMax = -FLT_MAX;
                for ( float z = z0 + step * 0.5f; z < z1; z += step ) {
                    for ( float x = x0 + step * 0.5f; x < x1; x += step ) {
                        const float gx = ( x - sector.Origin.x ) / spacing;
                        const float gz = ( z - sector.Origin.z ) / spacing;
                        if ( gx < 0.0f || gz < 0.0f || gx > 256.0f || gz > 256.0f ) {
                            continue; // footprint leaves this sector: its own bank carries that part
                        }
                        const float h = heights[static_cast<uint32_t>( gz ) * kHeightfieldSize +
                                                static_cast<uint32_t>( gx )];
                        numSamples++;
                        heightMin = Min( heightMin, h );
                        heightMax = Max( heightMax, h );
                        if ( h >= boundsMin.y - 0.25f && h <= boundsMax.y + 0.25f ) {
                            numBand++; // terrain inside the object's volume (the dirt class)
                        } else if ( h < boundsMin.y - 0.25f ) {
                            numBelow++; // terrain under the floor (the sinking-into-the-mouth look)
                        } else {
                            numAbove++;
                        }
                    }
                }
                const bool bHit = numBand != 0;
                if ( bHit ) {
                    numHits++;
                }
                OTDU_LOG_INFO( "scantunnels: %s %s '%s' (kw %s) x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f band %u below %u above %u of %u h %.1f..%.1f\n",
                               bHit ? "HIT" : "ok ", SectorName( id ).c_str(), nodes[i].Name.c_str(), kind, x0, x1,
                               boundsMin.y, boundsMax.y, z0, z1, numBand, numBelow, numAbove, numSamples, heightMin,
                               heightMax );
            }
            if ( numScanned % 64 == 0 ) {
                OTDU_LOG_INFO( "scantunnels: %u sectors scanned, %u banks missing, %u nodes, %u hits\n", numScanned,
                               numMissing, numNodes, numHits );
            }
            // Carve coverage — the owner's "holes in the geometry ... i thought
            // it was transparent i fell through ... near tunnels/bridges" (s86).
            // Over a carved cell the heightfield is not ground any more
            // (getGroundHeight answers through SampleCarveGround only): the
            // tunnel / bridge's up-facing triangles are. A carved cell with no
            // such surface over it is a hole - the sheet is drawn without depth
            // write over nothing, and the car falls through it. Measured here so
            // a hole comes out as a coordinate instead of an eyeball.
            if ( !sector.Terrain.Carved.empty() ) {
                // The carvers' footprints WITHOUT the one-cell margin
                // MarkCarvedCells adds (the sheet needs that ring so it never
                // z-fights the shell edge). A carved cell outside them is the
                // ring - expected. A carved cell INSIDE one with no floor is a
                // real gap.
                std::vector<std::pair<Vector3f, Vector3f>> footprints;
                for ( int32_t i = 0; i < static_cast<int32_t>( nodes.size() ); i++ ) {
                    if ( CarveKeyword( nodes, i ) == nullptr ) {
                        continue;
                    }
                    Vector3f boxMin, boxMax;
                    if ( sector.Objects.getNodeBounds( i, boxMin, boxMax ) ) {
                        footprints.push_back( { boxMin, boxMax } );
                    }
                }
                uint32_t numCarved = 0, numHoles = 0, numMargin = 0;
                float holeMinX = FLT_MAX, holeMaxX = -FLT_MAX, holeMinZ = FLT_MAX, holeMaxZ = -FLT_MAX;
                std::string firstHoles;
                // Where the gaps are, grouped on 8 m cells (the teleport list):
                // key -> { sum x, sum z, count }.
                struct HoleCluster {
                    double SumX = 0.0;
                    double SumZ = 0.0;
                    uint32_t Count = 0;
                };
                std::unordered_map<uint64_t, HoleCluster> clusters;
                for ( uint32_t cell = 0; cell < static_cast<uint32_t>( sector.Terrain.Carved.size() ); cell++ ) {
                    if ( sector.Terrain.Carved[cell] == 0 ) {
                        continue;
                    }
                    numCarved++;
                    if ( cell >= heights.size() ) {
                        continue;
                    }
                    const uint32_t gx = cell % kHeightfieldSize;
                    const uint32_t gz = cell / kHeightfieldSize;
                    const float localX = gx * spacing;
                    const float localZ = gz * spacing;
                    // Any up-facing surface below a very high origin counts:
                    // this asks "is there floor at all", not "is it reachable".
                    float ground = 0.0f;
                    if ( SampleCarveGround( sector, sector.Origin.x + localX, sector.Origin.z + localZ, heights[cell] + 1000.0f,
                                            ground ) ) {
                        continue;
                    }
                    bool bInsideFootprint = false;
                    for ( const auto &box : footprints ) {
                        if ( localX >= box.first.x && localX <= box.second.x && localZ >= box.first.z &&
                             localZ <= box.second.z ) {
                            bInsideFootprint = true;
                            break;
                        }
                    }
                    if ( !bInsideFootprint ) {
                        numMargin++; // the sheet's margin ring around the shell
                        continue;
                    }
                    numHoles++;
                    holeMinX = Min( holeMinX, sector.Origin.x + localX );
                    holeMaxX = Max( holeMaxX, sector.Origin.x + localX );
                    holeMinZ = Min( holeMinZ, sector.Origin.z + localZ );
                    holeMaxZ = Max( holeMaxZ, sector.Origin.z + localZ );
                    {
                        HoleCluster &cluster = clusters[CellKey( static_cast<int32_t>( localX / 8.0f ),
                                                                 static_cast<int32_t>( localZ / 8.0f ) )];
                        cluster.SumX += sector.Origin.x + localX;
                        cluster.SumZ += sector.Origin.z + localZ;
                        cluster.Count++;
                    }
                    if ( numHoles <= 3 ) {
                        char entry[48];
                        snprintf( entry, sizeof( entry ), "%s(%.0f,%.0f)", numHoles > 1 ? ", " : "",
                                  sector.Origin.x + localX, sector.Origin.z + localZ );
                        firstHoles += entry;
                    }
                }
                numCarvedTotal += numCarved;
                numUncoveredTotal += numHoles;
                if ( numHoles != 0 ) {
                    // The three biggest 8 m clusters: a teleport target per gap.
                    std::vector<std::pair<uint32_t, const std::pair<const uint64_t, HoleCluster> *>> biggest;
                    biggest.reserve( clusters.size() );
                    for ( const auto &entry : clusters ) {
                        biggest.push_back( { entry.second.Count, &entry } );
                    }
                    std::sort( biggest.begin(), biggest.end(),
                               []( const auto &a, const auto &b ) { return a.first > b.first; } );
                    std::string spots;
                    for ( size_t i = 0; i < biggest.size() && i < 3; i++ ) {
                        const HoleCluster &cluster = biggest[i].second->second;
                        char entry[64];
                        snprintf( entry, sizeof( entry ), "%s%.0fx%.0f(%u)", i != 0 ? " " : "",
                                  cluster.SumX / cluster.Count, cluster.SumZ / cluster.Count, cluster.Count );
                        spots += entry;
                    }
                    OTDU_LOG_WARN(
                        "scantunnels: HOLE %s: %u of %u carved cells have no tunnel / bridge floor under them "
                        "(x %.0f..%.0f z %.0f..%.0f) first %s; %u margin; spots %s\n",
                        SectorName( id ).c_str(), numHoles, numCarved, holeMinX, holeMaxX, holeMinZ, holeMaxZ,
                        firstHoles.c_str(), numMargin, spots.c_str() );
                } else if ( numCarved != 0 ) {
                    OTDU_LOG_INFO( "scantunnels: carve %s: %u cells, every one under a tunnel / bridge floor "
                                   "(%u margin cells have none, by design)\n",
                                   SectorName( id ).c_str(), numCarved, numMargin );
                }
            }
            const bool bKeep = keep.count( SectorRowColumn( id ) ) != 0;
            if ( !bKeep ) {
                // The same unload steps unloadFar takes (the model frees its
                // own GPU buffers in its destructor).
                gpRender->getDevice().destroyBuffer( sector.Terrain.pBuffer );
                destroyCarveBuffers( sector );
                gpRender->getTextureCache().releaseOwner( sector.BankPath.c_str() );
                for ( auto it = sectors.begin(); it != sectors.end(); ++it ) {
                    if ( ( *it )->Id == id ) {
                        sectors.erase( it );
                        break;
                    }
                }
                markAreasDirty( id );
            }
        }
    }
    OTDU_LOG_INFO( "scantunnels: done. %u sectors scanned, %u banks missing, %u keyword nodes, %u with terrain inside\n",
                   numScanned, numMissing, numNodes, numHits );
    // The hole summary: carved cells are the terrain the tunnel / bridge is
    // supposed to floor over. Uncovered ones are the see-through / fall-through
    // gaps; 0 means every carved cell has a floor.
    OTDU_LOG_INFO( "scantunnels: carve coverage: %u carved cells, %u uncovered (holes)\n", numCarvedTotal,
                   numUncoveredTotal );
}

void EdenWorld::updateIslandIndices() {
    // Island quads (384 m, 16 per area) not covered by a loaded area
    // heightfield [INF: the level LOD selection is encrypted].
    bIslandIndicesDirty = false;
    gpRender->getDevice().destroyBuffer( pIslandIndexBuffer );
    pIslandIndexBuffer = nullptr;
    numIslandIndices = 0;
    const uint32_t quadsPerArea = static_cast<uint32_t>( kSectorsPerArea * kSectorSize / 384.0f );
    std::vector<uint8_t> covered( kGridQuads * kGridQuads, 0 );
    for ( const auto &pArea : areas ) {
        if ( pArea->Terrain.Materials.empty() || pArea->X < 0 || pArea->Y < 0 ) {
            continue;
        }
        for ( uint32_t z = pArea->Y * quadsPerArea; z < ( pArea->Y + 1 ) * quadsPerArea && z < kGridQuads; z++ ) {
            for ( uint32_t x = pArea->X * quadsPerArea; x < ( pArea->X + 1 ) * quadsPerArea && x < kGridQuads; x++ ) {
                covered[z * kGridQuads + x] = 1;
            }
        }
    }
    std::vector<uint32_t> indices;
    for ( uint32_t z = 0; z < kGridQuads; z++ ) {
        for ( uint32_t x = 0; x < kGridQuads; x++ ) {
            if ( covered[z * kGridQuads + x] ) {
                continue;
            }
            const uint32_t i0 = z * kHeightfieldSize + x;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + kHeightfieldSize;
            const uint32_t i3 = i2 + 1;
            indices.insert( indices.end(), { i0, i2, i1, i1, i2, i3 } );
        }
    }
    if ( indices.empty() ) {
        return;
    }
    pIslandIndexBuffer = gpRender->getDevice().createBuffer( indices.size() * sizeof( uint32_t ),
                                                             VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices.data() );
    numIslandIndices = static_cast<uint32_t>( indices.size() );
}

bool EdenWorld::loadAround( const Vector3f &position, int32_t sectorRadius, int32_t maxSectors ) {
    if ( areas.empty() ) {
        for ( int32_t y = 0; y < areaRows; y++ ) {
            for ( int32_t x = 0; x < areaColumns; x++ ) {
                loadArea( x, y );
            }
        }
        OTDU_LOG_INFO( "EdenWorld: %zu areas loaded (%d x %d grid, the others have no bank)\n", areas.size(),
                       areaColumns, areaRows );
    }
    const SectorId center = getSectorAt( position );
    bool bLoadedAny = false;
    int32_t numLoaded = 0;
    const int32_t centerColumn = center.AreaX * kSectorsPerArea + center.X;
    const int32_t centerRow = center.AreaY * kSectorsPerArea + center.Y;
    // The sectors preloaded ahead (preloadOne) inside this square are now part
    // of the drawn world: the moment the synchronous streaming would have
    // loaded them.
    bool bRevealed = false;
    for ( auto &pSector : sectors ) {
        if ( !pSector->bPreloaded ) {
            continue;
        }
        const int32_t column = pSector->Id.AreaX * kSectorsPerArea + pSector->Id.X;
        const int32_t row = pSector->Id.AreaY * kSectorsPerArea + pSector->Id.Y;
        if ( std::abs( column - centerColumn ) <= sectorRadius && std::abs( row - centerRow ) <= sectorRadius ) {
            pSector->bPreloaded = false;
            markAreasDirty( pSector->Id ); // the area mesh drops its coarse cells now
            bRevealed = true;
        }
    }
    if ( bRevealed ) {
        bOceanVisibilityDirty = true;
        drawEpoch++;
    }
    for ( int32_t row = centerRow - sectorRadius; row <= centerRow + sectorRadius; row++ ) {
        for ( int32_t column = centerColumn - sectorRadius; column <= centerColumn + sectorRadius; column++ ) {
            if ( row < 0 || column < 0 ) {
                continue;
            }
            SectorId id;
            id.AreaX = column / kSectorsPerArea;
            id.AreaY = row / kSectorsPerArea;
            id.X = column % kSectorsPerArea;
            id.Y = row % kSectorsPerArea;
            const uint32_t key = static_cast<uint32_t>( row * 1024 + column );
            if ( !isSectorLoaded( id ) && failedSectors.count( key ) == 0 ) {
                if ( loadSector( id ) ) {
                    bLoadedAny = true;
                    numLoaded++;
                } else {
                    failedSectors.insert( key );
                }
                if ( maxSectors > 0 && numLoaded >= maxSectors ) {
                    // The caller's budget is spent: the rest of the square
                    // lands on the following frames' calls.
                    return bLoadedAny;
                }
            }
        }
    }
    if ( bLoadedAny ) {
        drawEpoch++;
        OTDU_LOG_INFO( "EdenWorld: %zu sectors loaded around (%.0f, %.0f)\n", sectors.size(), position.x,
                       position.z );
    }
    return bLoadedAny;
}

bool EdenWorld::preloadOne( const Vector3f &position, const Vector3f &ahead, int32_t sectorRadius ) {
    if ( areas.empty() ) {
        return false; // loadAround sets the world up first
    }
    const SectorId center = getSectorAt( position );
    const int32_t centerColumn = center.AreaX * kSectorsPerArea + center.X;
    const int32_t centerRow = center.AreaY * kSectorsPerArea + center.Y;
    SectorId best;
    uint32_t bestKey = 0;
    float bestDistance = FLT_MAX;
    for ( int32_t row = centerRow - sectorRadius; row <= centerRow + sectorRadius; row++ ) {
        for ( int32_t column = centerColumn - sectorRadius; column <= centerColumn + sectorRadius; column++ ) {
            if ( row < 0 || column < 0 ) {
                continue;
            }
            SectorId id;
            id.AreaX = column / kSectorsPerArea;
            id.AreaY = row / kSectorsPerArea;
            id.X = column % kSectorsPerArea;
            id.Y = row % kSectorsPerArea;
            const uint32_t key = static_cast<uint32_t>( row * 1024 + column );
            if ( failedSectors.count( key ) != 0 || isSectorLoaded( id ) ) {
                continue;
            }
            const Vector3f middle = getSectorOrigin( id ) + Vector3f( kSectorSize * 0.5f, 0.0f, kSectorSize * 0.5f );
            const float dx = middle.x - ahead.x, dz = middle.z - ahead.z;
            const float distance = dx * dx + dz * dz;
            if ( distance < bestDistance ) {
                bestDistance = distance;
                best = id;
                bestKey = key;
            }
        }
    }
    if ( bestDistance == FLT_MAX ) {
        return false;
    }
    if ( !loadSector( best ) ) {
        failedSectors.insert( bestKey );
        return true; // an attempt: the ring is not finished, try the next one next frame
    }
    sectors.back()->bPreloaded = true;
    static const bool bLog = CmdLine::HasArg( "-logsectortimes" );
    if ( bLog ) {
        OTDU_LOG_INFO( "[sector] preloaded %s (%zu loaded)\n", SectorName( best ).c_str(), sectors.size() );
    }
    return true;
}

void EdenWorld::prefetchAround( const Vector3f &position, int32_t sectorRadius ) {
    const SectorId center = getSectorAt( position );
    const int32_t centerColumn = center.AreaX * kSectorsPerArea + center.X;
    const int32_t centerRow = center.AreaY * kSectorsPerArea + center.Y;
    for ( int32_t row = centerRow - sectorRadius; row <= centerRow + sectorRadius; row++ ) {
        for ( int32_t column = centerColumn - sectorRadius; column <= centerColumn + sectorRadius; column++ ) {
            if ( row < 0 || column < 0 || failedSectors.count( static_cast<uint32_t>( row * 1024 + column ) ) != 0 ) {
                continue;
            }
            SectorId id;
            id.AreaX = column / kSectorsPerArea;
            id.AreaY = row / kSectorsPerArea;
            id.X = column % kSectorsPerArea;
            id.Y = row % kSectorsPerArea;
            if ( id.AreaX >= areaColumns || id.AreaY >= areaRows || isSectorLoaded( id ) ) {
                continue;
            }
            gpFile->prefetchFile( getSectorBankPath( id ).c_str() );
        }
    }
}

// A carved cell with no up-facing tunnel / bridge surface over it is a hole:
// the sheet is drawn without depth write over nothing (see-through) and the
// drive height function has no answer there (getGroundHeight -> this sampler),
// so the car falls through. Those cells keep their opaque terrain instead -
// the pre-carve look at that cell, and ground the car can stand on.
void EdenWorld::PruneUncoveredCarveCells( Sector &sector ) {
    const std::vector<float> &heights = sector.Terrain.Heights;
    if ( sector.Terrain.Carved.empty() || heights.empty() ) {
        return;
    }
    const float spacing = sector.Terrain.Spacing;
    uint32_t numPruned = 0;
    for ( uint32_t cell = 0; cell < static_cast<uint32_t>( sector.Terrain.Carved.size() ); cell++ ) {
        if ( sector.Terrain.Carved[cell] == 0 || cell >= heights.size() ) {
            continue;
        }
        const uint32_t gx = cell % kHeightfieldSize;
        const uint32_t gz = cell / kHeightfieldSize;
        const float x = sector.Origin.x + gx * spacing;
        const float z = sector.Origin.z + gz * spacing;
        float ground = 0.0f;
        // Any up-facing surface below a very high origin counts: the question
        // is "is there a floor", not "is it reachable from the car".
        if ( SampleCarveGround( sector, x, z, heights[cell] + 1000.0f, ground ) ) {
            continue;
        }
        sector.Terrain.Carved[cell] = 0;
        numPruned++;
    }
    if ( numPruned != 0 ) {
        OTDU_LOG_DEBUG( "EdenWorld: %s: %u carved cells kept their terrain (no tunnel / bridge floor under them)\n",
                        SectorName( sector.Id ).c_str(), numPruned );
    }
}

void EdenWorld::updateAreaIndices( Area &area ) {
    area.bIndicesDirty = false;
    gpRender->getDevice().destroyBuffer( area.pIndexBuffer );
    area.pIndexBuffer = nullptr;
    area.NumIndices = 0;

    // The cells covered by a loaded sector are not drawn by the area mesh
    // (the sector heightfield, 3 m, is drawn instead of the 24 m area grid).
    // The cell scan is 255 x 255, so the loaded set is hashed once instead of
    // walking the sector list per cell (this runs again whenever a sector is
    // loaded or unloaded, i.e. every streaming step).
    std::unordered_set<int32_t> loaded;
    loaded.reserve( sectors.size() * 2 );
    for ( const auto &pSector : sectors ) {
        if ( !pSector->bPreloaded ) { // hidden ahead sectors keep the area cells
            loaded.insert( SectorRowColumn( pSector->Id ) );
        }
    }

    const float spacing = area.Terrain.Spacing;
    std::vector<uint32_t> indices;
    indices.reserve( kGridQuads * kGridQuads * 6 );
    for ( uint32_t z = 0; z < kGridQuads; z++ ) {
        for ( uint32_t x = 0; x < kGridQuads; x++ ) {
            const Vector3f center( area.Origin.x + ( x + 0.5f ) * spacing, 0.0f, area.Origin.z + ( z + 0.5f ) * spacing );
            if ( loaded.count( SectorRowColumn( getSectorAt( center ) ) ) != 0 ) {
                continue;
            }
            const uint32_t i0 = z * kHeightfieldSize + x;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + kHeightfieldSize;
            const uint32_t i3 = i2 + 1;
            indices.insert( indices.end(), { i0, i2, i1, i1, i2, i3 } );
        }
    }
    if ( indices.empty() ) {
        return;
    }
    area.pIndexBuffer = gpRender->getDevice().createBuffer( indices.size() * sizeof( uint32_t ),
                                                            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices.data() );
    area.NumIndices = static_cast<uint32_t>( indices.size() );
}

void EdenWorld::updateCarveBuffers( Sector &sector ) {
    Heightfield &terrain = sector.Terrain;
    terrain.bCarveIndicesDirty = false;
    if ( terrain.Carved.empty() ) {
        return;
    }
    uint32_t numMarked = 0;
    for ( const uint8_t mark : terrain.Carved ) {
        numMarked += mark != 0 ? 1 : 0;
    }
    if ( numMarked == 0 ) {
        return; // nothing crossed: the full opaque grid stays
    }
    RenderDevice &device = gpRender->getDevice();
    destroyCarveBuffers( sector );
    std::vector<uint32_t> split, carved;
    split.reserve( numGridIndices );
    // Whole quads go to the carved buffer when any of their corners is
    // marked (a quad whose sheet crosses the tunnel wall would z-fight with
    // it half-drawn).
    for ( uint32_t z = 0; z < kGridQuads; z++ ) {
        for ( uint32_t x = 0; x < kGridQuads; x++ ) {
            const uint32_t i0 = z * kHeightfieldSize + x;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + kHeightfieldSize;
            const uint32_t i3 = i2 + 1;
            std::vector<uint32_t> &dst = ( terrain.Carved[i0] | terrain.Carved[i1] | terrain.Carved[i2] |
                                           terrain.Carved[i3] )
                                             ? carved
                                             : split;
            dst.insert( dst.end(), { i0, i2, i1, i1, i2, i3 } );
        }
    }
    if ( !split.empty() ) {
        terrain.pSplitIndices = device.createBuffer( split.size() * sizeof( uint32_t ),
                                                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT, split.data() );
        terrain.NumSplitIndices = static_cast<uint32_t>( split.size() );
    }
    if ( !carved.empty() ) {
        terrain.pCarvedIndices = device.createBuffer( carved.size() * sizeof( uint32_t ),
                                                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT, carved.data() );
        terrain.NumCarvedIndices = static_cast<uint32_t>( carved.size() );
        if ( terrain.bCarveMaterialDirty && terrain.Materials.size() == 1 ) {
            // The carved cells draw before everything else without depth
            // write (opaque blend, depth test on): the tunnel / bridge
            // geometry that crosses them paints over them.
            EdenMaterial carvedMaterial = terrain.Materials[0];
            carvedMaterial.bDepthWrite = false;
            carvedMaterial.Name += " carved";
            terrain.Materials.push_back( carvedMaterial );
            terrain.bCarveMaterialDirty = false;
        }
    }
}

void EdenWorld::destroyCarveBuffers( Sector &sector ) {
    RenderDevice &device = gpRender->getDevice();
    device.destroyBuffer( sector.Terrain.pSplitIndices );
    device.destroyBuffer( sector.Terrain.pCarvedIndices );
    sector.Terrain.pSplitIndices = sector.Terrain.pCarvedIndices = nullptr;
    sector.Terrain.NumSplitIndices = sector.Terrain.NumCarvedIndices = 0;
}

void EdenWorld::draw( EdenRenderer &renderer, const Vector3f &cameraPosition ) {
    static const bool bNoAreas = CmdLine::HasArg( "-noareas" );
    static const bool bNoTerrain = CmdLine::HasArg( "-noterrain" );
    static const bool bNoObjects = CmdLine::HasArg( "-noobjects" );
    static const bool bNoInstances = CmdLine::HasArg( "-noinstances" );
    static const bool bNoAreaObjects = CmdLine::HasArg( "-noareaobjects" );
    if ( bOceanVisibilityDirty ) {
        updateOceanVisibility();
    }
    // OpenTDU2 shore swell: the sea moves, everything else stays (the offset
    // is applied to TD_Ocean items only, see EdenModel::setOceanYOffset).
    islandOcean.setOceanYOffset( seaLevelOffset );
    if ( islandOcean.isLoaded() && !bNoObjects && bOceanVisible ) {
        islandOcean.draw( renderer, Matrix4x4f::Identity() );
    }
    if ( bIslandTerrainEnabled && !islandTerrain.Materials.empty() && !bNoAreas ) {
        if ( bIslandIndicesDirty ) {
            updateIslandIndices();
        }
        if ( numIslandIndices > 0 ) {
            EdenMesh mesh = islandTerrain.Mesh;
            mesh.pIndexBuffer = pIslandIndexBuffer;
            mesh.IndexCount = numIslandIndices;
            renderer.draw( mesh, islandTerrain.Materials[0], Matrix4x4f::Identity() );
        }
    }
    for ( auto &pArea : areas ) {
        if ( bNoAreas || !bAreasEnabled ) {
            break;
        }
        Area &area = *pArea;
        if ( area.bIndicesDirty ) {
            updateAreaIndices( area );
        }
        if ( area.NumIndices > 0 && !area.Terrain.Materials.empty() ) {
            EdenMesh mesh = area.Terrain.Mesh;
            mesh.pIndexBuffer = area.pIndexBuffer;
            mesh.IndexCount = area.NumIndices;
            // The area permutation (S_USERTYPE4 = 1) uses a 24 m grid.
            const Matrix4x4f world = Matrix4x4f::Translation( area.Origin );
            renderer.draw( mesh, area.Terrain.Materials[0], world );
        }
    }
    // Area objects (area-local -o: the 768 m ocean tile of each sector cell
    // and the far objects), grouped per sector by updateOceanVisibility. They
    // were loaded but never drawn on this branch: the island AREA-x-y tile is
    // hidden under a loaded area, so the sea beyond the loaded sector ring was
    // missing and popped as the ring moved. Group visibility draws the area
    // tile exactly where its sector tile is not resident.
    for ( auto &pArea : areas ) {
        if ( bNoObjects || bNoAreaObjects || !bAreasEnabled ) {
            break;
        }
        Area &area = *pArea;
        if ( area.Objects.isLoaded() ) {
            area.Objects.setOceanYOffset( seaLevelOffset );
            area.Objects.draw( renderer, Matrix4x4f::Translation( area.Origin ) );
        }
    }
    for ( auto &pSector : sectors ) {
        Sector &sector = *pSector;
        if ( sector.bPreloaded ) {
            continue; // loaded ahead, drawn once the streaming square reaches it
        }
        const Matrix4x4f world = Matrix4x4f::Translation( sector.Origin );
        if ( !sector.Terrain.Materials.empty() && !bNoTerrain ) {
            // The heightfield quads inside tunnel / bridge volumes draw
            // FIRST without depth write, so the tunnel / bridge geometry
            // paints over them (the retail TD_HeightMap is itself drawn
            // blended without depth write, layer table 0x00f7f604 [CODE];
            // the A/B that found the behaviour: -terrainblend -terrainearly,
            // docs/STATE.md session 42). Every other cell keeps the opaque
            // depth-writing draw (the session-22 see-through fix: hills
            // keep hiding what is behind them). -nocarve restores one full
            // opaque grid.
            if ( !sector.Terrain.Carved.empty() && sector.Terrain.bCarveIndicesDirty ) {
                updateCarveBuffers( sector );
            }
            EdenMesh mesh = sector.Terrain.Mesh;
            if ( sector.Terrain.NumCarvedIndices > 0 ) {
                EdenDrawParams immediate;
                mesh.pIndexBuffer = sector.Terrain.pCarvedIndices;
                mesh.IndexCount = sector.Terrain.NumCarvedIndices;
                renderer.draw( mesh, sector.Terrain.Materials[1], world, &immediate );
            }
            mesh.pIndexBuffer = sector.Terrain.pSplitIndices != nullptr ? sector.Terrain.pSplitIndices
                                                                        : sector.Terrain.Mesh.pIndexBuffer;
            mesh.IndexCount = sector.Terrain.pSplitIndices != nullptr ? sector.Terrain.NumSplitIndices
                                                                      : sector.Terrain.Mesh.IndexCount;
            renderer.draw( mesh, sector.Terrain.Materials[0], world, nullptr );
        }
        if ( sector.Objects.isLoaded() && !bNoObjects ) {
            // The sea rises by seaLevelOffset; the foam strip travels up the
            // beach gradient by the same height, plus that height itself so it
            // stays on the sand (see loadSector's FoamTideVector).
            sector.Objects.setOceanYOffset( seaLevelOffset );
            sector.Objects.setFoamOffset( sector.FoamTideVector * seaLevelOffset +
                                          Vector3f( 0.0f, seaLevelOffset, 0.0f ) );
            sector.Objects.draw( renderer, world );
        }
        if ( !bNoObjects && !bNoInstances ) {
            drawInstances( renderer, sector, cameraPosition );
        }
    }
    drawImpostors( renderer );
    drawGrass( renderer, cameraPosition );
}

bool EdenWorld::addImpostor( const EdenMaterial &material, const EdenModel::Item &item, const Matrix4x4f &instanceWorld,
                             const Vector3f &cameraPosition ) {
    // Camera-facing about the vertical axis: retail FUN_00996820 turns each
    // impostor by the camera angle (quantised to the impostor's view count,
    // cos / sin at 0x9968f0 / 0x996912) before submitting it [CODE]. The quad
    // is a YZ-plane card whose front faces -X (V_PPin_C: x = 0, y -9.8..37.7,
    // z +-23.8 [DATA: -logimpostors]); its yaw is replaced by the one that
    // turns that face to the camera, the instance's position and per-axis scale are kept. A proper
    // rotation, so the winding stays as authored. [INF: the view quantisation
    // (a texture atlas of views) is not reproduced - the textures hold one view.]
    Matrix4x4f world = instanceWorld;
    // Only the vertical card (all vertices at local x = 0, the YZ plane) is a
    // side view that turns to the camera; any other card of the impostor (a
    // horizontal one with the strip's top-down view) keeps the instance
    // transform and its own UVs. [INF: the cards' roles read from their plane
    // and the atlas layout.]
    bool bVerticalCard = false;
    {
        const RawVertexAttribute *pPos = nullptr;
        for ( const RawVertexAttribute &attribute : item.Mesh.Attributes ) {
            if ( attribute.Usage == 0 && attribute.UsageIndex == 0 && attribute.Stream < 2 ) {
                pPos = &attribute;
                break;
            }
        }
        if ( pPos != nullptr && ( pPos->Format == VAF_POSITION_R32G32B32_SFLOAT ||
                                  pPos->Format == VAF_POSITION_R32G32B32A32_SFLOAT ) ) {
            const std::vector<uint8_t> &positions = item.ImpostorStreams[pPos->Stream];
            const uint32_t positionStride = item.Mesh.Strides[pPos->Stream];
            bVerticalCard = positionStride != 0 && positions.size() >= positionStride;
            for ( size_t at = pPos->Offset; bVerticalCard && at + 12 <= positions.size(); at += positionStride ) {
                bVerticalCard = std::fabs( *reinterpret_cast<const float *>( positions.data() + at ) ) < 1e-3f;
            }
        }
    }
    // The texture is an atlas of views around the tree (V_PPin_C: the quad's U
    // spans 0..0.125 = 8 cells [DATA: -logimpostors]: 7 side views + a top
    // view); the shader passes the UV
    // through, so the cell is picked here, from the camera direction in the
    // instance's own frame snapped to the nearest view (retail quantises the
    // angle by the view count in FUN_00996820 [CODE]). [INF: which cell is which
    // angle - cell 0 = the camera on the card's front (-X) side, counting
    // counter-clockwise seen from above.]
    float cellOffset = 0.0f;
    {
        static const bool bNoAtlas = CmdLine::HasArg( "-noimpostoratlas" );
        const RawVertexAttribute *pUV = nullptr;
        for ( const RawVertexAttribute &attribute : item.Mesh.Attributes ) {
            if ( attribute.Usage == 5 && attribute.UsageIndex == 0 && attribute.Format == VAF_TEXCOORD_R32G32_SFLOAT &&
                 attribute.Stream < 2 ) {
                pUV = &attribute;
                break;
            }
        }
        if ( pUV != nullptr && !bNoAtlas && bVerticalCard ) {
            const std::vector<uint8_t> &uvStream = item.ImpostorStreams[pUV->Stream];
            const uint32_t uvStride = item.Mesh.Strides[pUV->Stream];
            float maxU = 0.0f;
            for ( size_t at = pUV->Offset; uvStride != 0 && at + 8 <= uvStream.size(); at += uvStride ) {
                maxU = Max( maxU, *reinterpret_cast<const float *>( uvStream.data() + at ) );
            }
            const int32_t cells = maxU > 0.0f ? static_cast<int32_t>( 1.0f / maxU + 0.5f ) : 1;
            if ( cells > 1 && cells <= 64 ) {
                const Vector3f toCamera( cameraPosition.x - instanceWorld.m[3][0], 0.0f,
                                         cameraPosition.z - instanceWorld.m[3][2] );
                const Vector3f axisX( instanceWorld.m[0][0], 0.0f, instanceWorld.m[0][2] );
                const Vector3f axisZ( instanceWorld.m[2][0], 0.0f, instanceWorld.m[2][2] );
                const float localX = toCamera.dot( axisX ), localZ = toCamera.dot( axisZ );
                // 0 with the camera on -X; increasing counter-clockwise.
                float angle = std::atan2( localZ, -localX );
                if ( angle < 0.0f ) {
                    angle += 6.28318530718f;
                }
                // The strip is cells - 1 side views around the tree plus a
                // top-down view in the last cell [DATA: v_ppin_c_h-i.2db 512x64
                // = 7 + 1, v_hpin_a_h-i.2db 2048x128 = 15 + 1]; a vertical card
                // only ever shows a side view.
                const int32_t sideViews = cells - 1;
                const int32_t cell = static_cast<int32_t>( angle / 6.28318530718f * sideViews + 0.5f ) % sideViews;
                cellOffset = static_cast<float>( cell ) / static_cast<float>( cells );
            }
        }
    }
    {
        auto rowLength = [&]( int32_t row ) {
            return std::sqrt( instanceWorld.m[row][0] * instanceWorld.m[row][0] +
                              instanceWorld.m[row][1] * instanceWorld.m[row][1] +
                              instanceWorld.m[row][2] * instanceWorld.m[row][2] );
        };
        const float sx = rowLength( 0 ), sy = rowLength( 1 ), sz = rowLength( 2 );
        float nx = cameraPosition.x - instanceWorld.m[3][0];
        float nz = cameraPosition.z - instanceWorld.m[3][2];
        const float length = std::sqrt( nx * nx + nz * nz );
        static const bool bNoBillboard = CmdLine::HasArg( "-noimpostorbillboard" );
        if ( length > 1e-3f && !bNoBillboard && bVerticalCard ) {
            // The card's front face is local -X (with +X toward the camera
            // the culled back face showed: every impostor vanished), so -X is
            // turned to the camera: X -> -n.
            nx = -nx / length;
            nz = -nz / length;
            // Rows = the images of the local axes (D3D row vectors): X -> n,
            // Y -> up, Z -> X x Y = (-nz, 0, nx).
            world.m[0][0] = nx * sx;
            world.m[0][1] = 0.0f;
            world.m[0][2] = nz * sx;
            world.m[1][0] = 0.0f;
            world.m[1][1] = sy;
            world.m[1][2] = 0.0f;
            world.m[2][0] = -nz * sz;
            world.m[2][1] = 0.0f;
            world.m[2][2] = nx * sz;
        }
    }
    const EdenMesh &mesh = item.Mesh;
    const RawVertexAttribute *pPosition = nullptr;
    for ( const RawVertexAttribute &attribute : mesh.Attributes ) {
        if ( attribute.Usage == 0 && attribute.UsageIndex == 0 ) { // D3DDECLUSAGE_POSITION
            pPosition = &attribute;
            break;
        }
    }
    if ( pPosition == nullptr || pPosition->Stream >= 2 ||
         ( pPosition->Format != VAF_POSITION_R32G32B32_SFLOAT &&
           pPosition->Format != VAF_POSITION_R32G32B32A32_SFLOAT ) ) {
        static bool bWarned = false;
        if ( !bWarned ) {
            bWarned = true;
            OTDU_LOG_WARN( "EdenWorld: impostor position format %u not batched (drawn per item)\n",
                           pPosition != nullptr ? static_cast<uint32_t>( pPosition->Format ) : 0xffu );
        }
        return false;
    }
    const uint32_t stride = mesh.Strides[pPosition->Stream];
    if ( stride == 0 || item.ImpostorStreams[pPosition->Stream].size() < stride ) {
        return false;
    }
    const uint32_t numVertices = static_cast<uint32_t>( item.ImpostorStreams[pPosition->Stream].size() / stride );
    // One batch per material and vertex layout (a pass draws ~60 of them).
    ImpostorBatch *pBatch = nullptr;
    for ( size_t b = 0; b < numImpostorBatches; b++ ) {
        ImpostorBatch &batch = impostorBatches[b];
        if ( batch.pMaterial == &material && batch.pLayout->LayoutKey == mesh.LayoutKey ) {
            pBatch = &batch;
            break;
        }
    }
    if ( pBatch == nullptr ) {
        if ( numImpostorBatches == impostorBatches.size() ) {
            impostorBatches.emplace_back();
        }
        pBatch = &impostorBatches[numImpostorBatches++];
        pBatch->pMaterial = &material;
        pBatch->pLayout = &mesh;
        pBatch->Streams[0].clear();
        pBatch->Streams[1].clear();
        pBatch->Indices.clear();
        pBatch->NumVertices = 0;
    }
    static const bool bLogImpostors = CmdLine::HasArg( "-logimpostors" );
    static std::unordered_set<const EdenModel::Item *> loggedImpostors;
    if ( bLogImpostors && loggedImpostors.insert( &item ).second ) {
        const std::vector<uint8_t> &src = item.ImpostorStreams[pPosition->Stream];
        std::string text;
        char part[96];
        for ( uint32_t v = 0; v < numVertices && v < 8; v++ ) {
            const float *pXYZ = reinterpret_cast<const float *>( src.data() + static_cast<size_t>( v ) * stride + pPosition->Offset );
            snprintf( part, sizeof( part ), " (%.2f %.2f %.2f)", pXYZ[0], pXYZ[1], pXYZ[2] );
            text += part;
        }
        for ( const RawVertexAttribute &attribute : mesh.Attributes ) {
            if ( attribute.Usage != 5 || attribute.Stream >= 2 || mesh.Strides[attribute.Stream] == 0 ) {
                continue; // D3DDECLUSAGE_TEXCOORD only
            }
            const std::vector<uint8_t> &uvStream = item.ImpostorStreams[attribute.Stream];
            snprintf( part, sizeof( part ), " | uv%u fmt %u:", attribute.UsageIndex, static_cast<uint32_t>( attribute.Format ) );
            text += part;
            for ( uint32_t v = 0; v < numVertices && v < 8; v++ ) {
                const uint8_t *pUV = uvStream.data() + static_cast<size_t>( v ) * mesh.Strides[attribute.Stream] + attribute.Offset;
                if ( attribute.Format == VAF_TEXCOORD_R32G32_SFLOAT ) {
                    const float *pF = reinterpret_cast<const float *>( pUV );
                    snprintf( part, sizeof( part ), " (%.3f %.3f)", pF[0], pF[1] );
                } else {
                    snprintf( part, sizeof( part ), " (%02x%02x %02x%02x)", pUV[1], pUV[0], pUV[3], pUV[2] );
                }
                text += part;
            }
        }
        text += bVerticalCard ? " [vertical card]" : " [other card]";
        OTDU_LOG_INFO( "EdenWorld: impostor '%s' alphaTest %d ref %u, %u vertices:%s; world row0 (%.2f %.2f %.2f) row1 (%.2f %.2f %.2f) row2 (%.2f %.2f %.2f)\n",
                       material.Name.c_str(), material.bAlphaTest ? 1 : 0, material.AlphaRef, numVertices, text.c_str(),
                       world.m[0][0], world.m[0][1], world.m[0][2], world.m[1][0], world.m[1][1], world.m[1][2],
                       world.m[2][0], world.m[2][1], world.m[2][2] );
    }
    const uint32_t base = pBatch->NumVertices;
    for ( uint32_t stream = 0; stream < 2; stream++ ) {
        const std::vector<uint8_t> &source = item.ImpostorStreams[stream];
        if ( mesh.Strides[stream] == 0 || source.empty() ) {
            continue;
        }
        std::vector<uint8_t> &target = pBatch->Streams[stream];
        const size_t start = target.size();
        target.insert( target.end(), source.begin(), source.begin() + static_cast<size_t>( numVertices ) * mesh.Strides[stream] );
        if ( cellOffset != 0.0f ) {
            for ( const RawVertexAttribute &attribute : mesh.Attributes ) {
                if ( attribute.Usage == 5 && attribute.UsageIndex == 0 && attribute.Stream == stream &&
                     attribute.Format == VAF_TEXCOORD_R32G32_SFLOAT ) {
                    for ( uint32_t v = 0; v < numVertices; v++ ) {
                        *reinterpret_cast<float *>( target.data() + start + static_cast<size_t>( v ) * mesh.Strides[stream] +
                                                    attribute.Offset ) += cellOffset;
                    }
                }
            }
        }
        if ( stream == pPosition->Stream ) {
            for ( uint32_t v = 0; v < numVertices; v++ ) {
                float *pXYZ = reinterpret_cast<float *>( target.data() + start + static_cast<size_t>( v ) * stride +
                                                         pPosition->Offset );
                const Vector3f p = world.transformPoint( Vector3f( pXYZ[0], pXYZ[1], pXYZ[2] ) );
                pXYZ[0] = p.x;
                pXYZ[1] = p.y;
                pXYZ[2] = p.z;
            }
        }
    }
    // A mirroring instance (negative determinant) flips the winding: the batch
    // draws with an identity world, so its triangles are reversed here.
    const float determinant = world.m[0][0] * ( world.m[1][1] * world.m[2][2] - world.m[1][2] * world.m[2][1] ) -
                              world.m[0][1] * ( world.m[1][0] * world.m[2][2] - world.m[1][2] * world.m[2][0] ) +
                              world.m[0][2] * ( world.m[1][0] * world.m[2][1] - world.m[1][1] * world.m[2][0] );
    const std::vector<uint32_t> &indices = item.ImpostorIndices;
    for ( size_t i = 0; i + 2 < indices.size(); i += 3 ) {
        if ( determinant < 0.0f ) {
            pBatch->Indices.push_back( base + indices[i] );
            pBatch->Indices.push_back( base + indices[i + 2] );
            pBatch->Indices.push_back( base + indices[i + 1] );
        } else {
            pBatch->Indices.push_back( base + indices[i] );
            pBatch->Indices.push_back( base + indices[i + 1] );
            pBatch->Indices.push_back( base + indices[i + 2] );
        }
    }
    pBatch->NumVertices += numVertices;
    return true;
}

// ---- Tall grass (docs/re/grass_spec.md) ------------------------------------

// Reads Key="Value" pairs of one XML element into a map (tolerant: the
// shipped grasstypes.xml is malformed, grass_spec.md 1.2).
static void GrassParseAttrs( const std::string &element, const char *pTag,
                             std::unordered_map<std::string, std::string> &out ) {
    const size_t tag = element.find( pTag );
    if ( tag == std::string::npos ) {
        return;
    }
    size_t p = tag + strlen( pTag );
    while ( p < element.size() ) {
        while ( p < element.size() && isspace( static_cast<unsigned char>( element[p] ) ) ) {
            p++;
        }
        const size_t keyStart = p;
        while ( p < element.size() && element[p] != '=' && !isspace( static_cast<unsigned char>( element[p] ) ) ) {
            p++;
        }
        if ( p >= element.size() || element[p] != '=' ) {
            break;
        }
        const std::string key = element.substr( keyStart, p - keyStart );
        p++;
        if ( p < element.size() && element[p] == '"' ) {
            p++;
            const size_t valueStart = p;
            while ( p < element.size() && element[p] != '"' ) {
                p++;
            }
            out[key] = element.substr( valueStart, p - valueStart );
            p++;
        }
    }
}

static float GrassAttrF( const std::unordered_map<std::string, std::string> &attrs, const char *pKey, float def ) {
    const auto it = attrs.find( pKey );
    return it != attrs.end() ? static_cast<float>( atof( it->second.c_str() ) ) : def;
}

static int32_t GrassAttrI( const std::unordered_map<std::string, std::string> &attrs, const char *pKey, int32_t def ) {
    const auto it = attrs.find( pKey );
    return it != attrs.end() ? static_cast<int32_t>( atoi( it->second.c_str() ) ) : def;
}

bool EdenWorld::loadGrassAssets( const char *pIsland ) {
    // grasstypes.xml: a loose file, and the shipped one is malformed (it ends
    // mid-tag) - a tolerant scan instead of an XML parser (grass_spec.md 1.2).
    std::vector<uint8_t> xml;
    const std::string xmlPath = gpFile->getBankFolder() + "islands/" + pIsland + "/fx/grasstypes.xml";
    if ( gpFile->loadFile( xmlPath.c_str(), xml ) ) {
        const std::string text( xml.begin(), xml.end() );
        size_t pos = 0;
        while ( true ) {
            const size_t type = text.find( "<GrassType ", pos );
            if ( type == std::string::npos ) {
                break;
            }
            const size_t end = text.find( "</GrassType>", type );
            const size_t stop = end == std::string::npos ? text.size() : end;
            const std::string element = text.substr( type, stop - type );
            pos = stop;
            std::unordered_map<std::string, std::string> attrs, grass;
            GrassParseAttrs( element, "<GrassType ", attrs );
            GrassParseAttrs( element, "<Grass ", grass );
            if ( attrs.find( "Name" ) == attrs.end() ) {
                continue;
            }
            GrassType t;
            t.Name = attrs["Name"];
            t.GrassBitmap = GrassAttrI( grass, "BitmapIndex", 0 );
            t.GrassVariation = GrassAttrI( grass, "TypeVariation", -1 );
            t.VariationProbability = GrassAttrF( grass, "VariationProbability", 0.0f );
            t.Height = GrassAttrF( grass, "Height", 0.6f );
            t.Width = GrassAttrF( grass, "Width", 2.0f );
            t.SizeDelta = GrassAttrF( grass, "SizeDelta", 0.0f );
            t.bNoShadow = GrassAttrI( grass, "NoShadow", 0 ) != 0;
            grassTypes.push_back( t );
        }
        OTDU_LOG_INFO( "EdenWorld: grass %zu types from %s\n", grassTypes.size(), xmlPath.c_str() );
    } else {
        OTDU_LOG_WARN( "EdenWorld: '%s' not found, grass falls back to one type\n", xmlPath.c_str() );
        grassTypes.push_back( GrassType{} );
    }
    // The card template + the atlases (grass_spec.md 1.1).
    const std::string grassPath = gpFile->getBankFolder() + "islands/" + pIsland + "/fx/tallgrass.bnk";
    if ( !grassBank.load( grassPath.c_str() ) ) {
        OTDU_LOG_WARN( "EdenWorld: '%s' not found, no grass\n", grassPath.c_str() );
        return false;
    }
    gpRender->getTextureCache().loadBankTextures( grassBank, grassPath.c_str() );
    if ( !grassModel.load( *pRenderer, grassBank, "tallgrass", nullptr, grassPath.c_str() ) ) {
        OTDU_LOG_WARN( "EdenWorld: failed to load the tallgrass card model\n" );
        return false;
    }
    // The grass materials declare kTD_Gouraud; the retail handler table
    // rewrites them to the TD_TallGrass family (grass_spec.md 4). v1 keeps
    // Gouraud (known-good: the cards draw textured and alpha-blended) and
    // -grasstallgrass opts into the rewrite - the TD_TallGrass VS is car-
    // anchored (CARPOS / CARFWD / CARRIGHT, no matrix multiply: it rebuilds
    // the positions in the car's basis, grass_spec.md 4) and its position
    // law is not lifted yet, so the rewritten materials resolve but draw
    // nothing visible. Scoped to this model on purpose: the rain shares
    // Gouraud, a handler-table entry would hijack it.
    static const bool bGrassTallGrass = CmdLine::HasArg( "-grasstallgrass" );
    for ( EdenMaterial &material : grassModel.getMaterials() ) {
        if ( bGrassTallGrass && material.ShaderSetGUID == 0x73078865ca37336dull /* kTD_Gouraud */ ) {
            material.ShaderSetGUID = 0xcafe8deca8c8cacaull; /* TD_TallGrass */
        }
        // FADEALFA is the retail distance fade the handler drives per frame
        // (the .2dm ships it 0 = fully transparent); v1 draws opaque and lets
        // the 140 m card radius do the fading [INF: fade law not lifted,
        // grass_spec.md 7].
        material.Parameters[GetIdentifier64bit( "FADEALFA" )] = Vector4f( 1.0f, 1.0f, 1.0f, 1.0f );
        if ( material.Name == "setgrass" ) {
            pGrassMaterial = &material;
        }
    }
    // Re-resolve the cached programs: the materials' pProgram was picked at
    // load with the declared kTD_Gouraud GUID, before the rewrite above.
    // The permutation a material picks is HashSpec of its spec bytes (2dm
    // +0x310, byte 0 = the runtime texture count) and TD_TallGrass ships 32
    // of them. The retail handler's exact switch law is [ENC] (grass_spec.md
    // 4); HashSpec is linear over GF(2), so the byte selection below was
    // solved from the shipped permutation ids (tools used: _grass/solve_spec3.py,
    // the method constant_pools.md 4) and verified: it hashes to permutation
    // fe58bfac59460e08 - the attrib-0 group's base variant [INF: the semantic
    // meaning of the selector bytes is not recovered, the hash match is].
    static const uint8_t kTallGrassSpec[EdenShaderSystem::kNumSpecs] = {
        0x01, 0xfb, 0x38, 0xe2, 0xb0, 0x12, 0x1b, 0xe8, 0xf2, 0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    for ( EdenMaterial &material : grassModel.getMaterials() ) {
        if ( material.ShaderSetGUID != 0xcafe8deca8c8cacaull ) {
            continue;
        }
        memcpy( material.Spec, kTallGrassSpec, sizeof( material.Spec ) );
        pRenderer->updateProgram( material );
        OTDU_LOG_INFO( "EdenWorld: grass '%s' resolved %s\n", material.Name.c_str(),
                       material.pProgram != nullptr ? material.pProgram->Name.c_str() : "NOTHING" );
    }
    for ( const EdenModel::Item &item : grassModel.getItems() ) {
        if ( item.Mesh.Strides[0] != 0 ) {
            grassLayout = item.Mesh;
            break;
        }
    }
    bGrassLoaded = pGrassMaterial != nullptr && grassLayout.Strides[0] != 0;
    OTDU_LOG_INFO( "EdenWorld: grass model %zu items, material '%s', %s\n", grassModel.getItems().size(),
                   pGrassMaterial != nullptr ? pGrassMaterial->Name.c_str() : "-",
                   bGrassLoaded ? "ready" : "INCOMPLETE" );
    return bGrassLoaded;
}

void EdenWorld::drawGrass( EdenRenderer &renderer, const Vector3f &cameraPosition ) {
    if ( !GrassEnabled() || !bGrassLoaded || grassTypes.empty() ) {
        return;
    }
    GrassBatch &batch = grassBatch;
    batch.Streams[0].clear();
    batch.Streams[1].clear();
    batch.Indices.clear();
    batch.NumVertices = 0;

    const float dist = GrassDistance();
    const float x0 = cameraPosition.x - dist;
    const float x1 = cameraPosition.x + dist;
    const float z0 = cameraPosition.z - dist;
    const float z1 = cameraPosition.z + dist;
    const float cell = kSectorSize / 1024.0f;  // 0.75 m per grid cell
    const int32_t stride = GrassStride();
    const float atlasCellU = 1.0f / 9.0f;      // 9 tuft cells over the 2048 px strip [INF]
    uint32_t cards = 0;
    const uint32_t maxCards = GrassMaxCards();

    bool bCapped = false;
    for ( const auto &pSector : sectors ) {
        if ( pSector->Pgr.empty() || pSector->bPreloaded || pSector->Terrain.Heights.empty() ) {
            continue;
        }
        const Vector3f &origin = pSector->Origin;
        const float sx0 = Max( x0, origin.x );
        const float sx1 = Min( x1, origin.x + kSectorSize );
        const float sz0 = Max( z0, origin.z );
        const float sz1 = Min( z1, origin.z + kSectorSize );
        if ( sx0 >= sx1 || sz0 >= sz1 ) {
            continue;
        }
        int32_t gx0 = Max( 0, static_cast<int32_t>( ( sx0 - origin.x ) / cell ) );
        int32_t gx1 = Min( 1023, static_cast<int32_t>( ( sx1 - origin.x ) / cell ) + 1 );
        int32_t gy0 = Max( 0, static_cast<int32_t>( ( sz0 - origin.z ) / cell ) );
        int32_t gy1 = Min( 1023, static_cast<int32_t>( ( sz1 - origin.z ) / cell ) + 1 );
        // The sampling lattice: one card every `stride` cells (grid axes ->
        // world X / Z [INF: the orientation A/B is -grassflip, grass_spec.md
        // 2 - the static correlation tests were inconclusive, the terrain
        // colour maps look area-level]).
        gx0 = ( ( gx0 + stride - 1 ) / stride ) * stride;
        gy0 = ( ( gy0 + stride - 1 ) / stride ) * stride;
        for ( int32_t gy = gy0; gy <= gy1 && !bCapped; gy += stride ) {
            const int32_t row = GrassFlip() ? 1023 - gy : gy;
            // Decode the row: u32 file offset at 0x28 + row * 4, then
            // (u8 count, u8 value) runs; (00, 00) pairs are no-ops; a run
            // may overshoot the row end (clamped at 1024 cells).
            uint32_t o;
            memcpy( &o, pSector->Pgr.data() + 0x28 + row * 4, 4 );
            if ( o + 2 > pSector->Pgr.size() ) {
                continue;
            }
            pgrRowScratch.assign( 1024, 0xff );
            uint32_t filled = 0;
            while ( filled < 1024 && o + 2 <= pSector->Pgr.size() ) {
                const uint32_t count = pSector->Pgr[o];
                const uint32_t value = pSector->Pgr[o + 1];
                o += 2;
                if ( count == 0 ) {
                    continue;
                }
                for ( uint32_t i = 0; i < count && filled < 1024; i++ ) {
                    pgrRowScratch[filled++] = static_cast<uint8_t>( value );
                }
            }
            for ( int32_t gx = gx0; gx <= gx1 && !bCapped; gx += stride ) {
                const uint32_t v = pgrRowScratch[gx];
                if ( v == 0x00 || v == 0xff ) {
                    continue;
                }
                const uint32_t hash = GrassHash( static_cast<uint32_t>( gx ), static_cast<uint32_t>( gy ) );
                const float f0 = ( hash & 1023 ) / 1023.0f;
                const float f1 = ( ( hash >> 10 ) & 1023 ) / 1023.0f;
                const float f2 = ( ( hash >> 20 ) & 1023 ) / 1023.0f;
                const float px = origin.x + ( gx + 0.5f + ( f0 - 0.5f ) * stride ) * cell;
                const float pz = origin.z + ( gy + 0.5f + ( f1 - 0.5f ) * stride ) * cell;
                float y;
                if ( !getHeight( px, pz, y ) ) {
                    continue;
                }
                const uint32_t typeIndex = Min<uint32_t>( v, static_cast<uint32_t>( grassTypes.size() ) - 1 );
                const GrassType &type = grassTypes[typeIndex];
                const float scale = 1.0f + type.SizeDelta * ( f2 * 2.0f - 1.0f );
                const float w = type.Width * scale;
                const float cardHeight = type.Height * scale;
                uint32_t bmp = static_cast<uint32_t>( Max( 0, type.GrassBitmap ) );
                if ( type.GrassVariation >= 0 &&
                     ( ( hash >> 28 ) & 255 ) / 255.0f < type.VariationProbability ) {
                    bmp = static_cast<uint32_t>( Max( 0, type.GrassVariation ) );
                }
                bmp = Min( bmp, 8u );
                const float u0 = bmp * atlasCellU;
                const float u1 = u0 + atlasCellU;
                const float yaw = f1 * 3.14159265f;
                const float dx = cosf( yaw ), dz = sinf( yaw );
                // Two crossed vertical quads (the PLANE template stood up,
                // grass_spec.md 5): bl, br, tl, tr per quad, triangle list,
                // the roots at v = 1 (the atlas fades them at the image
                // bottom [INF]).
                for ( int32_t quad = 0; quad < 2; quad++ ) {
                    const float qdx = quad == 0 ? dx : -dz;
                    const float qdz = quad == 0 ? dz : dx;
                    const float hx = qdx * w * 0.5f;
                    const float hz = qdz * w * 0.5f;
                    const float corners[4][5] = {
                        { px - hx, y,              pz - hz, u0, 1.0f },
                        { px + hx, y,              pz + hz, u1, 1.0f },
                        { px - hx, y + cardHeight, pz - hz, u0, 0.0f },
                        { px + hx, y + cardHeight, pz + hz, u1, 0.0f },
                    };
                    const uint32_t base = batch.NumVertices;
                    for ( const auto &corner : corners ) {
                        batch.Streams[0].insert( batch.Streams[0].end(), reinterpret_cast<const uint8_t *>( corner ),
                                                 reinterpret_cast<const uint8_t *>( corner ) + 20 );
                        const uint16_t normal[4] = { GrassFloatToHalf( 0.0f ), GrassFloatToHalf( 1.0f ),
                                                     GrassFloatToHalf( 0.0f ), GrassFloatToHalf( 0.0f ) };
                        batch.Streams[1].insert( batch.Streams[1].end(), reinterpret_cast<const uint8_t *>( normal ),
                                                 reinterpret_cast<const uint8_t *>( normal ) + 8 );
                    }
                    const uint32_t indices[6] = { base, base + 1, base + 2, base + 1, base + 3, base + 2 };
                    batch.Indices.insert( batch.Indices.end(), indices, indices + 6 );
                    batch.NumVertices += 4;
                }
                cards++;
                if ( cards >= maxCards ) {
                    bCapped = true;
                }
            }
        }
    }
    if ( batch.Indices.empty() ) {
        return;
    }
    if ( LogGrass() ) {
        OTDU_LOG_INFO( "EdenWorld: grass %u cards (%u tris) within %.0f m%s, material guid %016llx\n", cards,
                       static_cast<uint32_t>( batch.Indices.size() / 3 ), dist, bCapped ? " CAPPED" : "",
                       static_cast<unsigned long long>( pGrassMaterial->ShaderSetGUID ) );
    }
    // Transient upload + draw, the drawImpostors mechanics: the layout is
    // the PLANE_01 template (position + UV in stream 0, f16 normal in
    // stream 1, grass_spec.md 1.1).
    RenderDevice &device = gpRender->getDevice();
    EdenMesh mesh = grassLayout;
    bool bOk = true;
    for ( uint32_t stream = 0; stream < 2; stream++ ) {
        if ( batch.Streams[stream].empty() ) {
            continue;
        }
        const TransientAllocation allocation = device.allocateTransient( batch.Streams[stream].size(), 16 );
        if ( allocation.pData == nullptr ) {
            bOk = false;
            break;
        }
        memcpy( allocation.pData, batch.Streams[stream].data(), batch.Streams[stream].size() );
        mesh.pStreamBuffers[stream] = device.getTransientBuffer();
        mesh.StreamOffsets[stream] = allocation.Offset;
    }
    const TransientAllocation indices = device.allocateTransient( batch.Indices.size() * sizeof( uint32_t ), 16 );
    if ( !bOk || indices.pData == nullptr ) {
        return;
    }
    memcpy( indices.pData, batch.Indices.data(), batch.Indices.size() * sizeof( uint32_t ) );
    mesh.pIndexBuffer = device.getTransientBuffer();
    mesh.FirstIndex = static_cast<uint32_t>( indices.Offset / sizeof( uint32_t ) );
    mesh.IndexCount = static_cast<uint32_t>( batch.Indices.size() );
    mesh.VertexOffset = 0;
    mesh.BlasAddress = 0;
    // The TD_TallGrass VS is car-anchored: its symbol table carries CARPOS /
    // CARFWD / CARRIGHT (the player-car basis the retail renderer feeds per
    // frame) and .LPHADIS / .CALEDIS (the display fade distances; shipped
    // defaults 0 would divide by zero). v1 feeds the camera as the anchor and
    // a pass-through fade [INF: the VS disassembly is not lifted yet,
    // grass_spec.md 7].
    EdenDrawParams params;
    params.Vectors["CARPOS"] = Vector4f( cameraPosition.x, cameraPosition.y, cameraPosition.z, 1.0f );
    params.Vectors["CARFWD"] = Vector4f( 0.0f, 0.0f, 1.0f, 0.0f );
    params.Vectors["CARRIGHT"] = Vector4f( 1.0f, 0.0f, 0.0f, 0.0f );
    params.Vectors[".LPHADIS"] = Vector4f( 1.0f, 1.0f, 1.0f, 1.0f );
    params.Vectors[".CALEDIS"] = Vector4f( 0.0f, 0.0f, 0.0f, 0.0f );
    renderer.draw( mesh, *pGrassMaterial, Matrix4x4f::Identity(), &params );
}

void EdenWorld::drawImpostors( EdenRenderer &renderer ) {
    if ( numImpostorBatches == 0 ) {
        return;
    }
    RenderDevice &device = gpRender->getDevice();
    for ( size_t b = 0; b < numImpostorBatches; b++ ) {
        ImpostorBatch &batch = impostorBatches[b];
        if ( batch.Indices.empty() ) {
            continue;
        }
        EdenMesh mesh = *batch.pLayout;
        bool bOk = true;
        for ( uint32_t stream = 0; stream < 2; stream++ ) {
            if ( batch.Streams[stream].empty() ) {
                continue;
            }
            const TransientAllocation allocation = device.allocateTransient( batch.Streams[stream].size(), 16 );
            if ( allocation.pData == nullptr ) {
                bOk = false;
                break;
            }
            memcpy( allocation.pData, batch.Streams[stream].data(), batch.Streams[stream].size() );
            mesh.pStreamBuffers[stream] = device.getTransientBuffer();
            mesh.StreamOffsets[stream] = allocation.Offset;
        }
        const TransientAllocation indices = device.allocateTransient( batch.Indices.size() * sizeof( uint32_t ), 16 );
        if ( !bOk || indices.pData == nullptr ) {
            continue;
        }
        memcpy( indices.pData, batch.Indices.data(), batch.Indices.size() * sizeof( uint32_t ) );
        mesh.pIndexBuffer = device.getTransientBuffer();
        mesh.FirstIndex = static_cast<uint32_t>( indices.Offset / sizeof( uint32_t ) );
        mesh.IndexCount = static_cast<uint32_t>( batch.Indices.size() );
        mesh.VertexOffset = 0;
        mesh.BlasAddress = 0;
        // The impostor light (see impostorAmbient): Ambient_0 = the weather's
        // impostor_ambient, Ambient_1 = 0 [INF: the retail light object holds
        // one colour; the second ambient slot of the AmbLight set is unused].
        impostorParams.VertexVectors["__AmbLight_Ambient_0"] =
            Vector4f( impostorAmbient.x, impostorAmbient.y, impostorAmbient.z, 1.0f );
        impostorParams.VertexVectors["__AmbLight_Ambient_1"] = Vector4f( 0.0f, 0.0f, 0.0f, 0.0f );
        renderer.draw( mesh, *batch.pMaterial, Matrix4x4f::Identity(), &impostorParams );
    }
    numImpostorBatches = 0;
}

bool EdenWorld::getGroundHeight( float x, float z, float fromY, float &outHeight ) const {
    const SectorId id = getSectorAt( Vector3f( x, 0.0f, z ) );
    for ( const auto &pSector : sectors ) {
        if ( !( pSector->Id == id ) ) {
            continue;
        }
        if ( pSector->Terrain.Carved.empty() || pSector->Terrain.Heights.empty() ) {
            return getHeight( x, z, outHeight );
        }
        const float gx = Clamp( ( x - pSector->Origin.x ) / 3.0f, 0.0f, 255.999f );
        const float gz = Clamp( ( z - pSector->Origin.z ) / 3.0f, 0.0f, 255.999f );
        const uint32_t ix = static_cast<uint32_t>( gx );
        const uint32_t iz = static_cast<uint32_t>( gz );
        const bool bCarved =
            pSector->Terrain.Carved[iz * kHeightfieldSize + ix] != 0 ||
            pSector->Terrain.Carved[iz * kHeightfieldSize + ix + 1] != 0 ||
            pSector->Terrain.Carved[( iz + 1 ) * kHeightfieldSize + ix] != 0 ||
            pSector->Terrain.Carved[( iz + 1 ) * kHeightfieldSize + ix + 1] != 0;
        if ( !bCarved ) {
            return getHeight( x, z, outHeight );
        }
        // The sheet is carved away here: the tunnel / bridge structure is the
        // ground (the bore floor the player sees). Where the sector's Havok
        // road triangles cover the query the raycast hits them first anyway;
        // this serves the slots and lay-bys the .shk does not carry.
        return SampleCarveGround( *pSector, x, z, fromY, outHeight );
    }
    return false;
}

int32_t EdenWorld::findLibraryNode( uint64_t hashcode ) const {
    const auto it = libraryNodes.find( hashcode );
    return it != libraryNodes.end() ? it->second : -1;
}

bool EdenWorld::getLibraryNodeBounds( int32_t nodeIndex, Vector3f &outMin, Vector3f &outMax ) const {
    if ( nodeIndex < 0 || !libraryModel.isLoaded() ) {
        return false;
    }
    return libraryModel.getNodeBounds( nodeIndex, outMin, outMax );
}

void EdenWorld::drawLibraryNode( EdenRenderer &renderer, int32_t nodeIndex, const Matrix4x4f &world ) const {
    if ( nodeIndex < 0 || !libraryModel.isLoaded() ) {
        return;
    }
    libraryModel.drawNodeItems( renderer, nodeIndex, world );
}

bool EdenWorld::getSurfaceHeight( float x, float z, float maxHeight, float &outHeight ) const {
    bool bFound = getHeight( x, z, outHeight ) && outHeight <= maxHeight;
    for ( const auto &pSector : sectors ) {
        float height = 0.0f;
        if ( pSector->Objects.getSurfaceHeight( x - pSector->Origin.x, z - pSector->Origin.z,
                                                maxHeight - pSector->Origin.y, height ) ) {
            height += pSector->Origin.y;
            if ( !bFound || height > outHeight ) {
                outHeight = height;
                bFound = true;
            }
        }
    }
    return bFound;
}

bool EdenWorld::getDriveHeight( float x, float z, float fromY, float headroom, float &outHeight ) const {
    const float maxHeight = fromY + headroom;
    bool bFound = getHeight( x, z, outHeight ) && outHeight <= maxHeight;
    const SectorId id = getSectorAt( Vector3f( x, 0.0f, z ) );
    for ( const auto &pSector : sectors ) {
        if ( !( pSector->Id == id ) ) {
            continue;
        }
        float height = 0.0f;
        if ( pSector->Objects.getSurfaceHeight( x - pSector->Origin.x, z - pSector->Origin.z,
                                                maxHeight - pSector->Origin.y, height ) ) {
            height += pSector->Origin.y;
            if ( !bFound || height > outHeight ) {
                outHeight = height;
                bFound = true;
            }
        }
        break; // one sector carries the point
    }
    return bFound;
}

bool EdenWorld::isSheltered( float x, float z, float fromY, float maxDist ) const {
    // A carved cell IS the inside of a tunnel / under a bridge deck: the
    // heightfield is gone there (getHeight refuses the cell), so the volume
    // itself answers "yes". This is also the cheap path: no collision walk.
    const SectorId id = getSectorAt( Vector3f( x, 0.0f, z ) );
    for ( const auto &pSector : sectors ) {
        if ( !( pSector->Id == id ) || pSector->Terrain.Heights.empty() ) {
            continue;
        }
        if ( !pSector->Terrain.Carved.empty() ) {
            const float gx = Clamp( ( x - pSector->Origin.x ) / 3.0f, 0.0f, 255.999f );
            const float gz = Clamp( ( z - pSector->Origin.z ) / 3.0f, 0.0f, 255.999f );
            const uint32_t ix = static_cast<uint32_t>( gx );
            const uint32_t iz = static_cast<uint32_t>( gz );
            if ( pSector->Terrain.Carved[iz * kHeightfieldSize + ix] != 0 ||
                 pSector->Terrain.Carved[iz * kHeightfieldSize + ix + 1] != 0 ||
                 pSector->Terrain.Carved[( iz + 1 ) * kHeightfieldSize + ix] != 0 ||
                 pSector->Terrain.Carved[( iz + 1 ) * kHeightfieldSize + ix + 1] != 0 ) {
                return true;
            }
        }
        break;
    }
    // A surface of the sector objects above the eye (a building's floor, a
    // canopy, a covered passage). getSurfaceHeight answers the HIGHEST surface
    // below the limit, so a roof over the point wins over the ground under it.
    float ceiling = 0.0f;
    if ( getSurfaceHeight( x, z, fromY + maxDist, ceiling ) && ceiling > fromY + 1.5f ) {
        return true;
    }
    return false;
}

bool EdenWorld::getHeight( float x, float z, float &outHeight ) const {
    const SectorId id = getSectorAt( Vector3f( x, 0.0f, z ) );
    for ( const auto &pSector : sectors ) {
        if ( !( pSector->Id == id ) || pSector->Terrain.Heights.empty() ) {
            continue;
        }
        const float gx = Clamp( ( x - pSector->Origin.x ) / 3.0f, 0.0f, 255.999f );
        const float gz = Clamp( ( z - pSector->Origin.z ) / 3.0f, 0.0f, 255.999f );
        const uint32_t ix = static_cast<uint32_t>( gx );
        const uint32_t iz = static_cast<uint32_t>( gz );
        // The cells carved out of the opaque terrain (inside a tunnel / bridge
        // volume, MarkCarvedCells) hold no ground here either. Session 56's
        // carve was render-only: the sheet kept colliding, so a car in a tunnel
        // still rode the invisible dirt ramp up to its mouth (reproduced
        // headless: the underpass bore, y 31.8 -> 35.2 over 80 m). The same
        // whole-quad rule the render split uses - any carved corner disables
        // the sample - and the car stands on the sector's Havok road triangles
        // instead (the tunnel roads ship in the -R.shk).
        if ( !pSector->Terrain.Carved.empty() &&
             ( pSector->Terrain.Carved[iz * kHeightfieldSize + ix] != 0 ||
               pSector->Terrain.Carved[iz * kHeightfieldSize + ix + 1] != 0 ||
               pSector->Terrain.Carved[( iz + 1 ) * kHeightfieldSize + ix] != 0 ||
               pSector->Terrain.Carved[( iz + 1 ) * kHeightfieldSize + ix + 1] != 0 ) ) {
            return false;
        }
        const float fx = gx - ix, fz = gz - iz;
        const std::vector<float> &h = pSector->Terrain.Heights;
        const float h00 = h[iz * kHeightfieldSize + ix], h10 = h[iz * kHeightfieldSize + ix + 1];
        const float h01 = h[( iz + 1 ) * kHeightfieldSize + ix], h11 = h[( iz + 1 ) * kHeightfieldSize + ix + 1];
        outHeight = ( h00 * ( 1 - fx ) + h10 * fx ) * ( 1 - fz ) + ( h01 * ( 1 - fx ) + h11 * fx ) * fz;
        return true;
    }
    return false;
}

void EdenWorld::updateWetGround( EdenRenderer &renderer, const WeatherPreset *pWeather, GPUTexture *pEnvironmentCube,
                                 float deltaTime ) {
    const float intensity = pWeather != nullptr ? pWeather->getFloat( "rain_ground_global_intensity", 0.0f ) : 0.0f;
    const float wet = pWeather != nullptr ? pWeather->getFloat( "rain_ground_wet", 0.0f ) : 0.0f;
    const float puddleHeight = pWeather != nullptr ? pWeather->getFloat( "rain_ground_puddle_height", 0.0f ) : 0.0f;
    const float rippleIntensity =
        pWeather != nullptr ? pWeather->getFloat( "rain_ground_ripple_intensity", 0.0f ) : 0.0f;
    const float rippleScale = pWeather != nullptr ? pWeather->getFloat( "rain_ground_ripple_scale", 0.6f ) : 0.6f;
    const float rippleSpeed =
        pWeather != nullptr ? pWeather->getFloat( "rain_ground_ripple_scroll_speed", 0.0f ) : 0.0f;
    // FUN_00aa0b60: scroll (same on u and v), FUN_00aa0440: ripple frame
    // every 0.025 s.
    rippleScroll += deltaTime * rippleSpeed * 0.15f;
    if ( rippleTimer < 0.0f ) {
        rippleFrame = ( rippleFrame + 1 ) & 15;
        rippleTimer = 0.025f;
    } else {
        rippleTimer -= deltaTime;
    }
    const bool bEnable = intensity > 0.0f && pEnvironmentCube != nullptr && pPuddles != nullptr;
    // The rain switch is silent otherwise: one line the first time it comes on
    // (a headless run has no other way to tell the wet ground happened).
    static bool bLoggedWet = false;
    if ( bEnable && !bLoggedWet ) {
        bLoggedWet = true;
        OTDU_LOG_INFO( "EdenWorld: wet ground on (intensity %.2f, wet %.2f, puddle %.2f)\n", intensity, wet, puddleHeight );
    }
    static const uint64_t kRainSpecs = GetIdentifier64bit( "RainSpecs" );
    static const uint64_t kRainRippleSpecs = GetIdentifier64bit( "RainRippleSpecs" );
    for ( auto &pSector : sectors ) {
        for ( EdenMaterial &material : pSector->Objects.getMaterials() ) {
            if ( material.ShaderSetGUID != 0xc8ad93c95455291bull && material.ShaderSetGUID != 0xe8ad93c00455291bull ) {
                continue;
            }
            // FUN_00aa0780: S_USERTYPE7 materials never get the rain switch.
            if ( material.Spec[38] != 0 ) {
                continue;
            }
            if ( bEnable ) {
                material.Parameters[kRainSpecs] = Vector4f( intensity, wet, 1.0f, puddleHeight );
                material.Parameters[kRainRippleSpecs] =
                    Vector4f( rippleScroll, rippleScroll, rippleIntensity, rippleScale );
                GPUTexture *pRipple = pRipples[rippleFrame] != nullptr ? pRipples[rippleFrame] : pRipples[0];
                const bool bWasWet = material.Spec[40] == 1 && material.NumSlots >= 9;
                material.pTextures[6] = pRipple;
                material.pTextures[7] = pEnvironmentCube;
                material.pTextures[8] = pPuddles;
                // The frame swaps behind a stable material: keep the stamp the
                // draw-set cache keys on in step, or every wet material shares
                // the first ripple frame bound in the frame slot (trap 13).
                material.TextureHashes[6] =
                    pRipple != nullptr ? GetIdentifier64bit( pRipple->Name.c_str() ) : 0;
                if ( !bWasWet ) {
                    material.NumSlots = Max<uint32_t>( material.NumSlots, 9 );
                    material.TextureHashes[6] = GetIdentifier64bit( "RIPPLE_01" );
                    material.TextureHashes[7] = GetIdentifier64bit( "ENVMAP" );
                    material.TextureHashes[8] = GetIdentifier64bit( "PUDDLES" );
                    material.Spec[40] = 1;
                    renderer.updateProgram( material );
                    if ( material.pProgram == nullptr ) {
                        OTDU_LOG_WARN( "EdenWorld: no rain permutation for '%s'\n", material.Name.c_str() );
                        material.Spec[40] = 0;
                        for ( uint32_t i = 6; i < 9; i++ ) {
                            material.pTextures[i] = nullptr;
                            material.TextureHashes[i] = 0;
                        }
                        renderer.updateProgram( material );
                    }
                }
            } else if ( material.Spec[40] == 1 ) {
                material.Spec[40] = 0;
                for ( uint32_t i = 6; i < 9; i++ ) {
                    material.pTextures[i] = nullptr;
                    material.TextureHashes[i] = 0;
                }
                renderer.updateProgram( material );
            }
        }
    }
}

namespace {
GPUTexture *FindColorMap( const std::vector<EdenMaterial> &materials ) {
    static const uint64_t kColor = GetIdentifier64bit( "COLOR" );
    if ( materials.empty() ) {
        return nullptr;
    }
    for ( uint32_t i = 0; i < materials[0].NumSlots; i++ ) {
        if ( materials[0].SlotTypes[i] == kColor ) {
            return materials[0].pTextures[i];
        }
    }
    return nullptr;
}

bool IsOcean( const EdenMaterial &material ) { return material.ShaderSetGUID == 0x0cea000000000002ull; }
} // namespace

void EdenWorld::updateOceanVisibility() {
    // Finest level wins (ocean_spec 5.2 [INF]): the island AREA-x-y tiles of
    // areas that have their own ocean tiles and the area tiles of loaded
    // sectors are hidden. The three levels are coplanar (y = 0 [DATA]), so a
    // tile left under a finer one z-fights with it.
    // [DATA ibiza.bnk ibiza-o.3dd] the island AREA-x-y nodes are dummies
    // (no object); the mesh hangs on their child (OCEAN, OCEAO, ...), so the
    // hiding applies to every item under the AREA-x-y node, not to the node
    // itself (hiding the AREA node alone hid nothing: the island ocean was
    // always drawn under the area / sector oceans).
    bOceanVisibilityDirty = false;
    uint32_t islandHidden = 0, areaHidden = 0;
    if ( islandOcean.isLoaded() ) {
        const Render3DD &scene = islandOcean.getScene();
        const std::vector<SceneNode> &nodes = scene.getNodes();
        for ( size_t i = 0; i < nodes.size(); i++ ) {
            islandOcean.setNodeVisible( static_cast<int32_t>( i ), bOceanVisible );
        }
        for ( size_t i = 0; i < nodes.size(); i++ ) {
            int ax = -1, ay = -1;
            if ( sscanf( ToUpper( nodes[i].Name ).c_str(), "AREA-%d-%d", &ax, &ay ) != 2 ) {
                continue;
            }
            bool bCovered = false;
            for ( const auto &pArea : areas ) {
                if ( pArea->X != ax || pArea->Y != ay || !pArea->Objects.isLoaded() ) {
                    continue;
                }
                for ( const EdenModel::Item &item : pArea->Objects.getItems() ) {
                    bCovered |= IsOcean( pArea->Objects.getMaterials()[item.MaterialIndex] );
                }
            }
            if ( !bCovered ) {
                continue;
            }
            for ( const EdenModel::Item &item : islandOcean.getItems() ) {
                if ( scene.isDescendantOf( item.NodeIndex, static_cast<int32_t>( i ) ) ) {
                    islandOcean.setNodeVisible( item.NodeIndex, false );
                    islandHidden++;
                }
            }
        }
    }
    // Area objects: the items of a "Sector-ax-ay-sx-sy" group (its 768 m
    // ocean tile and the far objects) are hidden while that sector is loaded
    // (finest level wins [INF: the per sector table of FUN_00992820 is filled
    // in readable code, its use per frame is not]). The groups make the hide
    // exact: an area tile disappears only under the sector that supersedes
    // it, so the sea never loses a cell as the sector ring moves (the old
    // centre-sector guess hid a 6144 m area tile while a 768 m sector tile
    // replaced it - sea holes that popped in and out).
    auto sectorHasOceanTile = [this]( const SectorId &id ) {
        for ( const auto &pSector : sectors ) {
            if ( !( pSector->Id == id ) || pSector->bPreloaded || !pSector->Objects.isLoaded() ) {
                continue;
            }
            for ( const EdenModel::Item &item : pSector->Objects.getItems() ) {
                if ( IsOcean( pSector->Objects.getMaterials()[item.MaterialIndex] ) ) {
                    return true;
                }
            }
        }
        return false;
    };
    for ( auto &pArea : areas ) {
        Area &area = *pArea;
        if ( !area.Objects.isLoaded() ) {
            continue;
        }
        const std::vector<EdenModel::Item> &items = area.Objects.getItems();
        for ( size_t i = 0; i < items.size(); i++ ) {
            const EdenModel::Item &item = items[i];
            SectorId sector = i < area.ItemSectors.size() ? area.ItemSectors[i] : SectorId{ INT32_MIN, 0, 0, 0 };
            if ( sector.AreaX == INT32_MIN ) {
                const Vector3f center = area.Objects.getNodeTransform( item.NodeIndex )
                                            .transformPoint( ( item.BoundsMin + item.BoundsMax ) * 0.5f );
                sector = getSectorAt( area.Origin + center );
            }
            // A sector loaded ahead but not drawn yet (preloadOne) does not
            // supersede the area's coarse items: they stay until it shows.
            const bool bSectorLoaded = isSectorShown( sector );
            const bool bOcean = IsOcean( area.Objects.getMaterials()[item.MaterialIndex] );
            // An ocean tile is superseded only by a loaded sector that has
            // its own TD_Ocean tile; a loaded land sector leaves the area
            // tile as the only water there.
            const bool bSuperseded = bSectorLoaded && sectorHasOceanTile( sector );
            areaHidden += ( bOcean && bSuperseded ) ? 1 : 0;
            area.Objects.setNodeVisible(
                item.NodeIndex, bOcean ? ( bOceanVisible && !bSuperseded ) : !bSectorLoaded );
        }
    }
    for ( auto &pSector : sectors ) {
        Sector &sector = *pSector;
        if ( !sector.Objects.isLoaded() ) {
            continue;
        }
        for ( const EdenModel::Item &item : sector.Objects.getItems() ) {
            if ( IsOcean( sector.Objects.getMaterials()[item.MaterialIndex] ) ) {
                sector.Objects.setNodeVisible( item.NodeIndex, bOceanVisible );
            }
        }
    }
    if ( getenv( "OTDU_OCEAN_LOG" ) != nullptr ) {
        OTDU_LOG_INFO( "EdenWorld: ocean: %u/%zu island items hidden, %u area tiles hidden\n", islandHidden,
                       islandOcean.getItems().size(), areaHidden );
    }
}

void EdenWorld::forEachOceanMaterial(
    const std::function<void( EdenMaterial &material, uint8_t kind, GPUTexture *pColorMap )> &callback ) {
    for ( auto &pSector : sectors ) {
        GPUTexture *pColorMap = FindColorMap( pSector->Terrain.Materials );
        for ( EdenMaterial &material : pSector->Objects.getMaterials() ) {
            if ( IsOcean( material ) ) {
                callback( material, 0, pColorMap );
            }
        }
    }
    for ( auto &pArea : areas ) {
        GPUTexture *pColorMap = FindColorMap( pArea->Terrain.Materials );
        for ( EdenMaterial &material : pArea->Objects.getMaterials() ) {
            if ( IsOcean( material ) ) {
                callback( material, 1, pColorMap );
            }
        }
    }
    for ( EdenMaterial &material : islandOcean.getMaterials() ) {
        if ( IsOcean( material ) ) {
            callback( material, 2, pIslandColorMap );
        }
    }
}
