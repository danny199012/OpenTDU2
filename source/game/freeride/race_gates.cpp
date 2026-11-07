#include "shared.h"
#include "race_gates.h"

#include "core/xmb.h"
#include "filesystem/gs_file.h"
#include "game/cinematic/cinematic_audio.h"
#include "render/eden/eden_renderer.h"
#include "render/eden/eden_scene_view.h"
#include "render/gs_render.h"
#include "render/texture_cache.h"

namespace {
const char *ModelName( RaceGates::eModel model ) {
    switch ( model ) {
    case RaceGates::eModel::Trajectory: return "trajectory";
    case RaceGates::eModel::BrakingZone: return "braking zone";
    case RaceGates::eModel::Start: return "start";
    case RaceGates::eModel::Finish: return "finish";
    case RaceGates::eModel::Checkpoint: return "checkpoint";
    case RaceGates::eModel::Barrels: return "barrels";
    case RaceGates::eModel::Radar: return "radar";
    default: return "?";
    }
}
} // namespace

bool RaceGates::load( EdenSceneView &view, const std::string &island ) {
    unload();
    EdenRenderer *pRenderer = gpRender != nullptr ? gpRender->getEdenRenderer() : nullptr;
    path = gpFile->getBankFolder() + "islands/" + island + "/scenarics/packs/pack_cp.bnk";
    if ( pRenderer == nullptr || !bank.load( path.c_str() ) ) {
        OTDU_LOG_WARN( "RaceGates: '%s' could not be loaded\n", path.c_str() );
        return false;
    }
    gpRender->getTextureCache().loadBankTextures( bank, bank.getPath().c_str() );
    BankHashcodes hashcodes;
    hashcodes.load( bank );
    const BankEntry *pEntities = bank.findEntry( "entities.xmb" );
    XmbDocument entities;
    if ( pEntities == nullptr || !entities.load( pEntities->pData, pEntities->Size ) ) {
        OTDU_LOG_WARN( "RaceGates: '%s' has no entities.xmb\n", path.c_str() );
        return false;
    }
    const Render2DM *pLibrary = &view.getWorld().getLibraryMaterials();
    // An entity's model: its mesh[] entry of rscSubType 0 is the scene (3DD),
    // the 3DG / 2DM share its name (SpotExterior::load).
    auto loadPart = [&]( XmbValue actor, Part &outPart ) -> bool {
        uint64_t rsc = 0;
        for ( uint32_t j = 0; j < actor.getArraySize( "mesh" ); j++ ) {
            XmbValue mesh = actor.getArrayElement( "mesh", j );
            if ( mesh["rscSubType"].asInt() == 0 ) {
                rsc = mesh["rsc"].asUInt64();
                break;
            }
        }
        const BankEntry *pScene = rsc != 0 ? hashcodes.find( rsc, 0, 0 ) : nullptr;
        if ( pScene == nullptr ) {
            return false;
        }
        const BankEntry *pGeometry = bank.findEntry( pScene->Name.c_str(), "3dg" );
        const BankEntry *pMaterials = bank.findEntry( pScene->Name.c_str(), "2dm" );
        auto pModel = std::make_unique<EdenModel>();
        if ( pGeometry == nullptr ||
             !pModel->load( *pRenderer, *pScene, *pGeometry, pMaterials, pLibrary, bank.getPath().c_str() ) ) {
            return false;
        }
        pModel->bindUVAnimations( bank );
        view.registerNeonMaterials( *pModel );
        Matrix4x4f rootInverse;
        for ( const SceneNode &node : pModel->getScene().getNodes() ) {
            if ( node.Parent < 0 ) {
                rootInverse = node.Local.inverse();
                break;
            }
        }
        outPart.Local = rootInverse;
        outPart.MinX = pModel->getBoundsMin().x;
        outPart.Width = pModel->getBoundsMax().x - pModel->getBoundsMin().x;
        outPart.MinZ = pModel->getBoundsMin().z;
        outPart.Length = pModel->getBoundsMax().z - pModel->getBoundsMin().z;
        OTDU_LOG_INFO( "RaceGates: %s: bounds (%.2f, %.2f, %.2f) - (%.2f, %.2f, %.2f)\n", pScene->Name.c_str(),
                       pModel->getBoundsMin().x, pModel->getBoundsMin().y, pModel->getBoundsMin().z,
                       pModel->getBoundsMax().x, pModel->getBoundsMax().y, pModel->getBoundsMax().z );
        outPart.Model = std::move( pModel );
        return true;
    };
    XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
    // The plain actors (the barrels Gate_Main points at) by hashcode.
    uint64_t barrelActor = 0;
    for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__Actor__CheckPoint" ); i++ ) {
        XmbValue checkpoint = entityExport.getArrayElement( "ArrayOf_Entity__Actor__CheckPoint", i );
        const int32_t type = checkpoint["CheckPointType"].asInt();
        const int32_t offRoad = checkpoint["OffRoadCheckpoint"].asInt();
        const std::string name = checkpoint["__Name"].asString();
        eModel model = eModel::Count;
        // The road variants (OffRoadCheckpoint 0) [DATA: the record names].
        if ( offRoad == 0 ) {
            switch ( type ) {
            case 1: model = eModel::Start; break;
            case 2: model = eModel::Finish; break;
            case 3: model = eModel::Checkpoint; break;
            case 4: model = eModel::Radar; break;
            case 7: model = eModel::BrakingZone; break;
            case 6: barrelActor = checkpoint["Actor1"].asUInt64(); break;
            default: break;
            }
        }
        if ( model == eModel::Count ) {
            continue;
        }
        Part &part = parts[static_cast<int32_t>( model )];
        if ( !part.Model && !loadPart( checkpoint, part ) ) {
            OTDU_LOG_WARN( "RaceGates: %s (type %d, the %s) has no model\n", name.c_str(), type, ModelName( model ) );
        }
    }
    if ( CmdLine::HasArg( "-logracegates" ) ) {
        for ( const char *pArray : { "ArrayOf_Entity__Actor", "ArrayOf_Entity__Actor__CheckPoint" } ) {
            for ( uint32_t i = 0; i < entityExport.getArraySize( pArray ); i++ ) {
                XmbValue actor = entityExport.getArrayElement( pArray, i );
                std::string meshes;
                for ( uint32_t j = 0; j < actor.getArraySize( "mesh" ); j++ ) {
                    XmbValue mesh = actor.getArrayElement( "mesh", j );
                    const BankEntry *pEntry = hashcodes.find( mesh["rsc"].asUInt64(), 0, 0 );
                    meshes += " " + ( pEntry != nullptr ? pEntry->Name : std::string( "?" ) ) + "/" +
                              std::to_string( mesh["rscSubType"].asInt() );
                }
                OTDU_LOG_INFO( "RaceGates: %s '%s' hc %016llx type %d zone %016llx a1 %016llx a2 %016llx mesh%s\n", pArray,
                               actor["__Name"].asString().c_str(), (unsigned long long)actor["__Hc"].asUInt64(),
                               actor["CheckPointType"].asInt(), (unsigned long long)actor["ActivationZone"].asUInt64(),
                               (unsigned long long)actor["Actor1"].asUInt64(), (unsigned long long)actor["Actor2"].asUInt64(),
                               meshes.c_str() );
            }
        }
    }
    for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__Actor" ); i++ ) {
        XmbValue actor = entityExport.getArrayElement( "ArrayOf_Entity__Actor", i );
        if ( actor["__Hc"].asUInt64() == barrelActor && barrelActor != 0 &&
             !parts[static_cast<int32_t>( eModel::Barrels )].Model ) {
            loadPart( actor, parts[static_cast<int32_t>( eModel::Barrels )] );
        }
        // The racing line strip: the plain actor 'Trajectory' [DATA: the name].
        if ( actor["__Name"].asString() == "Trajectory" ) {
            loadPart( actor, parts[static_cast<int32_t>( eModel::Trajectory )] );
        }
    }
    int32_t numLoaded = 0;
    for ( const Part &part : parts ) {
        numLoaded += part.Model ? 1 : 0;
    }
    bLoaded = numLoaded > 0;
    OTDU_LOG_INFO( "RaceGates: %s: %d of %d gate models\n", path.c_str(), numLoaded, static_cast<int32_t>( eModel::Count ) );
    return bLoaded;
}

void RaceGates::unload() {
    for ( Part &part : parts ) {
        part = Part();
    }
    gates.clear();
    markers.clear();
    if ( bLoaded && gpRender != nullptr ) {
        gpRender->getTextureCache().releaseOwner( bank.getPath().c_str() );
    }
    bank.unload();
    bLoaded = false;
}

float RaceGates::getModelLength( eModel model ) const {
    const Part &part = parts[static_cast<int32_t>( model )];
    return part.Model ? part.Length : 0.0f;
}

float RaceGates::getModelWidth( eModel model ) const {
    const Part &part = parts[static_cast<int32_t>( model )];
    return part.Model ? part.Width : 0.0f;
}

void RaceGates::draw( EdenRenderer &renderer ) const {
    if ( !bLoaded ) {
        return;
    }
    for ( const Gate &gate : gates ) {
        // The gate frame: x across the road, z along the gate's normal.
        const float heading = std::atan2( gate.Forward.x, gate.Forward.z );
        const Matrix4x4f frame =
            Matrix4x4f::Translation( gate.Position ) *
            Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), heading ) );
        if ( gate.Model == eModel::Barrels ) {
            // One barrel on each edge of the gate (Gate_Main: Actor1 / Actor2);
            // a slalom pole is one barrel.
            const Part &barrel = parts[static_cast<int32_t>( eModel::Barrels )];
            if ( !barrel.Model ) {
                continue;
            }
            if ( gate.bSinglePole ) {
                barrel.Model->draw( renderer, frame * barrel.Local );
                continue;
            }
            for ( float side : { -1.0f, 1.0f } ) {
                const Matrix4x4f at = frame * Matrix4x4f::Translation( Vector3f( side * gate.Width * 0.5f, 0.0f, 0.0f ) );
                barrel.Model->draw( renderer, at * barrel.Local );
            }
            continue;
        }
        const Part &part = parts[static_cast<int32_t>( gate.Model )];
        if ( !part.Model ) {
            continue;
        }
        // The arch spans the gate: stretched across only (the posts keep
        // their height) [INF: the retail scale is ENC].
        const float scale = part.Width > 0.5f ? Clamp( gate.Width / part.Width, 0.5f, 4.0f ) : 1.0f;
        part.Model->draw( renderer, frame * Matrix4x4f::Scale( Vector3f( scale, 1.0f, 1.0f ) ) * part.Local );
    }
    for ( const Marker &marker : markers ) {
        const Part &part = parts[static_cast<int32_t>( marker.Model )];
        if ( !part.Model ) {
            continue;
        }
        Matrix4x4f frame =
            Matrix4x4f::Translation( marker.Position ) *
            Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), marker.Heading ) );
        if ( marker.Length > 0.0f || marker.Width > 0.0f ) {
            // A strip: its near end on the position, stretched to the length.
            const float sz = marker.Length > 0.0f && part.Length > 0.01f ? marker.Length / part.Length : 1.0f;
            const float sx = marker.Width > 0.0f && part.Width > 0.01f ? marker.Width / part.Width : 1.0f;
            frame = frame * Matrix4x4f::Scale( Vector3f( sx, 1.0f, sz ) ) *
                    Matrix4x4f::Translation( Vector3f( 0.0f, 0.0f, -part.MinZ ) );
        }
        part.Model->draw( renderer, frame * part.Local );
    }
}
