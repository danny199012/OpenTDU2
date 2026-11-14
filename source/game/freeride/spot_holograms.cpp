#include "shared.h"
#include "spot_holograms.h"

#include "core/xmb.h"
#include "filesystem/gs_file.h"
#include "game/cinematic/cinematic_audio.h"
#include "render/eden/eden_renderer.h"
#include "render/eden/eden_scene_view.h"
#include "render/gs_render.h"
#include "render/texture_cache.h"
#include "system/cmd_line.h"

bool SpotHolograms::load( EdenSceneView &view ) {
    unload();
    EdenRenderer *pRenderer = gpRender != nullptr ? gpRender->getEdenRenderer() : nullptr;
    const std::string path = gpFile->getBankFolder() + "scenarics/packs/pack_spots.bnk";
    if ( pRenderer == nullptr || !bank.load( path.c_str() ) ) {
        OTDU_LOG_WARN( "SpotHolograms: '%s' could not be loaded\n", path.c_str() );
        return false;
    }
    gpRender->getTextureCache().loadBankTextures( bank, bank.getPath().c_str() );
    BankHashcodes hashcodes;
    hashcodes.load( bank );
    const BankEntry *pEntities = bank.findEntry( "entities.xmb" );
    XmbDocument entities;
    if ( pEntities == nullptr || !entities.load( pEntities->pData, pEntities->Size ) ) {
        OTDU_LOG_WARN( "SpotHolograms: '%s' has no entities.xmb\n", path.c_str() );
        return false;
    }
    const Render2DM *pLibrary = &view.getWorld().getLibraryMaterials();
    const bool bLog = CmdLine::HasArg( "-logspotholo" );
    XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
    for ( const char *pArray : { "ArrayOf_Entity__Actor__SpotMesh", "ArrayOf_Entity__Actor__SpotMeshAddOn" } ) {
        const bool bAddOn = strstr( pArray, "AddOn" ) != nullptr;
        for ( uint32_t i = 0; i < entityExport.getArraySize( pArray ); i++ ) {
            XmbValue actor = entityExport.getArrayElement( pArray, i );
            Model model;
            model.Name = actor["__Name"].asString();
            model.bAddOn = bAddOn;
            // The Type condition (VIRTUAL if / then, +144 of the record): the
            // raw words until the XMB reader resolves VIRTUAL fields.
            for ( uint32_t w = 0; w < 4; w++ ) {
                model.TypeRaw[w] = entities.readU32( actor.getOffset() + 144 + w * 4 );
            }
            // The mesh[] entry of rscSubType 0 is the scene (3DD); the 3DG /
            // 2DM share its name (RaceGates::load, SpotExterior::load).
            uint64_t rsc = 0;
            for ( uint32_t j = 0; j < actor.getArraySize( "mesh" ); j++ ) {
                XmbValue mesh = actor.getArrayElement( "mesh", j );
                if ( mesh["rscSubType"].asInt() == 0 ) {
                    rsc = mesh["rsc"].asUInt64();
                    break;
                }
            }
            const BankEntry *pScene = rsc != 0 ? hashcodes.find( rsc, 0, 0 ) : nullptr;
            if ( pScene != nullptr ) {
                model.Scene = pScene->Name;
                const BankEntry *pGeometry = bank.findEntry( pScene->Name.c_str(), "3dg" );
                const BankEntry *pMaterials = bank.findEntry( pScene->Name.c_str(), "2dm" );
                auto pMesh = std::make_unique<EdenModel>();
                if ( pGeometry != nullptr &&
                     pMesh->load( *pRenderer, *pScene, *pGeometry, pMaterials, pLibrary, bank.getPath().c_str() ) ) {
                    pMesh->bindUVAnimations( bank );
                    view.registerNeonMaterials( *pMesh );
                    for ( const SceneNode &node : pMesh->getScene().getNodes() ) {
                        if ( node.Parent < 0 ) {
                            model.Local = node.Local.inverse();
                            break;
                        }
                    }
                    model.BoundsMin = pMesh->getBoundsMin();
                    model.BoundsMax = pMesh->getBoundsMax();
                    model.Mesh = std::move( pMesh );
                }
            }
            if ( bLog ) {
                OTDU_LOG_INFO( "SpotHolograms: %s[%u] '%s' scene '%s' type %08x %08x %08x %08x bounds (%.2f %.2f %.2f)-(%.2f "
                               "%.2f %.2f)%s\n",
                               bAddOn ? "AddOn" : "Mesh", i, model.Name.c_str(), model.Scene.c_str(), model.TypeRaw[0],
                               model.TypeRaw[1], model.TypeRaw[2], model.TypeRaw[3], model.BoundsMin.x, model.BoundsMin.y,
                               model.BoundsMin.z, model.BoundsMax.x, model.BoundsMax.y, model.BoundsMax.z,
                               model.Mesh ? "" : " NO MODEL" );
            }
            models.push_back( std::move( model ) );
        }
    }
    int32_t numLoaded = 0;
    for ( const Model &model : models ) {
        numLoaded += model.Mesh ? 1 : 0;
    }
    bLoaded = numLoaded > 0;
    OTDU_LOG_INFO( "SpotHolograms: %s: %d of %zu hologram models\n", path.c_str(), numLoaded, models.size() );
    return bLoaded;
}

void SpotHolograms::unload() {
    models.clear();
    placements.clear();
    if ( bLoaded && gpRender != nullptr ) {
        gpRender->getTextureCache().releaseOwner( bank.getPath().c_str() );
    }
    bank.unload();
    bLoaded = false;
}

int32_t SpotHolograms::findModel( const char *pName ) const {
    for ( size_t i = 0; i < models.size(); i++ ) {
        if ( StringCmpI( models[i].Name.c_str(), pName ) == 0 ) {
            return static_cast<int32_t>( i );
        }
    }
    return -1;
}

void SpotHolograms::draw( EdenRenderer &renderer ) const {
    if ( !bLoaded ) {
        return;
    }
    for ( const Placement &placement : placements ) {
        if ( placement.ModelIndex < 0 || placement.ModelIndex >= static_cast<int32_t>( models.size() ) ) {
            continue;
        }
        const Model &model = models[placement.ModelIndex];
        if ( !model.Mesh ) {
            continue;
        }
        const Matrix4x4f frame =
            Matrix4x4f::Translation( placement.Position ) * Matrix4x4f::Scale( Vector3f( placement.bMirror ? -placement.Scale : placement.Scale, placement.Scale, placement.Scale ) ) *
            Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), placement.Heading ) );
        model.Mesh->draw( renderer, frame * model.Local );
    }
}
