#include "shared.h"
#include "spot_exteriors.h"

#include "filesystem/gs_file.h"
#include "render/gs_render.h"

#include <algorithm>

namespace {
float HorizontalDistance( const Vector3f &a, const Vector3f &b ) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt( dx * dx + dz * dz );
}
} // namespace

std::vector<SpotRecord> LoadSpotList( const std::string &island ) {
    std::vector<SpotRecord> spots;
    std::vector<uint8_t> data;
    const std::string path = gpFile->getBankFolder() + "islands/" + island + "/level/spots.bin";
    if ( !gpFile->loadFile( path.c_str(), data ) || data.size() < 0x20 || memcmp( data.data() + 0x10, "SPTA", 4 ) != 0 ) {
        OTDU_LOG_WARN( "SpotList: '%s' not found or not a spot list\n", path.c_str() );
        return spots;
    }
    uint32_t size = 0;
    memcpy( &size, data.data() + 0x14, 4 );
    const size_t end = Min<size_t>( data.size(), 0x20 + static_cast<size_t>( size ) );
    for ( size_t offset = 0x20; offset + 0x110 <= end; offset += 0x110 ) {
        SpotRecord spot;
        memcpy( &spot.Id, data.data() + offset, 8 );
        char bank[9] = {};
        memcpy( bank, data.data() + offset + 8, 8 );
        spot.Bank = bank;
        memcpy( &spot.Position, data.data() + offset + 0x10, 12 );
        memcpy( &spot.Heading, data.data() + offset + 0x20, 4 );
        // The exterior bank: the record's bank name lowered (the file names are
        // lowercase), and only when the file is actually there (the game data
        // of this copy has most of them).
        if ( !spot.Bank.empty() ) {
            const std::string exterior =
                gpFile->getBankFolder() + "islands/" + island + "/exterior/" + ToLower( spot.Bank ) + ".bnk";
            if ( gpFile->fileExists( exterior.c_str() ) ) {
                spot.ExteriorPath = exterior;
            }
        }
        spots.push_back( spot );
    }
    return spots;
}

bool SpotExteriors::load( const std::string &island ) {
    unload();
    spots = LoadSpotList( island );
    bHasTicked = false;
    bLastEnabled = true;
    size_t withExterior = 0;
    for ( const SpotRecord &spot : spots ) {
        withExterior += spot.ExteriorPath.empty() ? 0 : 1;
    }
    OTDU_LOG_INFO( "SpotExteriors: %s: %zu spots, %zu with an exterior bank\n", island.c_str(), spots.size(),
                   withExterior );
    return !spots.empty();
}

void SpotExteriors::setSpots( std::vector<SpotRecord> list ) {
    unload();
    spots = std::move( list );
    size_t withExterior = 0;
    for ( const SpotRecord &spot : spots ) {
        withExterior += spot.ExteriorPath.empty() ? 0 : 1;
    }
    OTDU_LOG_INFO( "SpotExteriors: %zu spots, %zu with an exterior bank\n", spots.size(), withExterior );
}

void SpotExteriors::excludeBank( const std::string &bankPath ) {
    if ( bankPath.empty() ) {
        return;
    }
    excluded.push_back( bankPath );
    OTDU_LOG_INFO( "SpotExteriors: '%s' excluded (owned by the mode)\n", bankPath.c_str() );
}

void SpotExteriors::unload() {
    if ( !exteriors.empty() && gpRender != nullptr ) {
        // The models hold GPU resources and the lightmap list holds material
        // pointers: drain the device before they go.
        gpRender->getDevice().waitIdle();
    }
    exteriors.clear();
    spots.clear();
    bHasTicked = false;
}

bool SpotExteriors::tick( EdenSceneView &view, const Vector3f &position, bool bEnabled ) {
    if ( spots.empty() ) {
        return false;
    }
    // The list is small but the loop is per frame: only re-evaluate when the
    // caller moved (the load / unload band is 500 m wide, so 10 m is plenty) —
    // or when the last call hit the per-tick load cap.
    if ( bHasTicked && !bPendingLoads && bEnabled == bLastEnabled &&
         HorizontalDistance( position, lastPosition ) < kRetestDistance ) {
        return false;
    }
    lastPosition = position;
    bHasTicked = true;
    bLastEnabled = bEnabled;

    // Drop the banks whose spots are all beyond the unload distance (or all of
    // them when the feature is off). Lightmaps and shadow casters hold material
    // pointers, so both caches are rebuilt when a bank goes. The models hold
    // GPU resources: the same drain-before-release rule as unload() below and
    // the car switch (gm_freeride.cpp) - a model freed while a frame still
    // uses it wedges the device.
    if ( !exteriors.empty() && gpRender != nullptr ) {
        bool bAnyDropping = false;
        for ( const auto &pExterior : exteriors ) {
            bool bKeep = false;
            for ( const SpotRecord &spot : spots ) {
                bKeep |= bEnabled && spot.ExteriorPath == pExterior->getPath() &&
                         HorizontalDistance( spot.Position, position ) < kUnloadDistance;
            }
            bAnyDropping |= !bKeep;
        }
        if ( bAnyDropping ) {
            gpRender->getDevice().waitIdle();
            if ( EdenRenderer *pRenderer = gpRender->getEdenRenderer() ) {
                pRenderer->clearRtInstances();
            }
        }
    }
    bool bUnloaded = false;
    for ( auto it = exteriors.begin(); it != exteriors.end(); ) {
        bool bKeep = false;
        float nearDistance = FLT_MAX;
        for ( const SpotRecord &spot : spots ) {
            if ( spot.ExteriorPath != ( *it )->getPath() ) {
                continue;
            }
            const float distance = HorizontalDistance( spot.Position, position );
            nearDistance = Min( nearDistance, distance );
            bKeep |= bEnabled && distance < kUnloadDistance;
        }
        if ( bKeep ) {
            ++it;
        } else {
            OTDU_LOG_INFO( "SpotExteriors: '%s' dropped (%.0f m, unload at %.0f)\n", ( *it )->getPath().c_str(),
                           nearDistance, kUnloadDistance );
            it = exteriors.erase( it );
            bUnloaded = true;
        }
    }
    if ( bUnloaded ) {
        view.clearLightmapMaterials();
        for ( auto &pExterior : exteriors ) {
            pExterior->registerLightmaps( view );
        }
        if ( EdenRenderer *pRenderer = gpRender->getEdenRenderer() ) {
            pRenderer->clearShadowCasterCache();
        }
    }
    if ( !bEnabled ) {
        bPendingLoads = false;
        return bUnloaded;
    }
    int32_t numLoaded = 0;
    for ( const SpotRecord &spot : spots ) {
        if ( spot.ExteriorPath.empty() || HorizontalDistance( spot.Position, position ) >= kLoadDistance ) {
            continue;
        }
        if ( std::find( excluded.begin(), excluded.end(), spot.ExteriorPath ) != excluded.end() ) {
            continue; // drawn by the mode itself (the FREERIDE house cinematic)
        }
        bool bLoaded = false;
        for ( const auto &pExterior : exteriors ) {
            bLoaded |= pExterior->getPath() == spot.ExteriorPath;
        }
        if ( bLoaded ) {
            continue;
        }
        if ( numLoaded >= kMaxLoadsPerTick ) {
            break; // the rest on the next frame (bPendingLoads)
        }
        auto pExterior = std::make_unique<SpotExterior>();
        if ( !pExterior->load( view, spot.ExteriorPath ) ) {
            continue;
        }
        // Every spot of this bank: T(position) R_y(heading) (FirstOutro).
        for ( const SpotRecord &other : spots ) {
            if ( other.ExteriorPath == spot.ExteriorPath ) {
                pExterior->getFrames().push_back(
                    Matrix4x4f::Translation( other.Position ) *
                    Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), other.Heading ) ) );
            }
        }
        OTDU_LOG_INFO( "SpotExteriors: %s at (%.0f, %.0f): %zu models, %zu placements\n", spot.Bank.c_str(),
                       spot.Position.x, spot.Position.z, pExterior->getNumModels(), pExterior->getFrames().size() );
        exteriors.push_back( std::move( pExterior ) );
        numLoaded++;
    }
    // The cap stopped the loop with banks still to load: re-evaluate on the
    // next frame even if the caller did not move.
    bPendingLoads = numLoaded >= kMaxLoadsPerTick;
    return bUnloaded;
}

void SpotExteriors::draw( EdenRenderer &renderer ) const {
    for ( const auto &pExterior : exteriors ) {
        pExterior->draw( renderer );
    }
}
