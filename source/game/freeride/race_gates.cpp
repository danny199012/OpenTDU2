#include "shared.h"
#include "race_gates.h"

#include "core/xmb.h"
#include "filesystem/gs_file.h"
#include "game/cinematic/cinematic_audio.h"
#include "render/eden/eden_renderer.h"
#include "render/eden/eden_scene_view.h"
#include "render/gs_render.h"
#include "render/texture_cache.h"
#include "system/cmd_line.h"
#include "world/eden_world.h"

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
        if ( name == "CP_OffRoad" ) {
            model = eModel::CheckpointOff; // the pad tests' amber "C" pin gate [DATA: the record name; 360 captures]
        }
        // The road variants (OffRoadCheckpoint 0) [DATA: the record names].
        if ( offRoad == 0 && model == eModel::Count ) {
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
    // The separators (RS_Separ_A_act / _B_act) from the world's worldactors bank [DATA: licenses_spec 12.1].
    pSeparator[0] = pSeparator[1] = nullptr;
    {
        EdenWorld &world = view.getWorld();
        const char *names[2] = { "RS_Separ_A_act", "RS_Separ_B_act" };
        for ( int32_t v = 0; v < 2; v++ ) {
            for ( const auto &entry : world.getActorNames() ) {
                if ( StringCmpI( entry.second.c_str(), names[v] ) != 0 ) {
                    continue;
                }
                EdenModel *pModel = world.getActorModelPtr( entry.first );
                if ( pModel != nullptr && pModel->isLoaded() ) {
                    pSeparator[v] = pModel;
                    if ( v == 0 ) {
                        separatorMin = pModel->getBoundsMin();
                        separatorMax = pModel->getBoundsMax();
                    }
                    OTDU_LOG_INFO( "RaceGates: %s: bounds (%.2f, %.2f, %.2f) - (%.2f, %.2f, %.2f)\n", names[v], pModel->getBoundsMin().x,
                                   pModel->getBoundsMin().y, pModel->getBoundsMin().z, pModel->getBoundsMax().x, pModel->getBoundsMax().y,
                                   pModel->getBoundsMax().z );
                }
                break;
            }
        }
    }
    // The DEC2 line markers (types 0 / 1 / 2): the dev build's school code maps
    // the decor type to the world actor SF_Stud_act [CODE: dev 0x9795a0 jump
    // table: 0/1/2 -> TAC_SF_Stud_act, 3/4 -> RS_Separ_A_act, 5 -> RS_Separ_B_act].
    // The ss_pwor_a roadwork actor is the second candidate [INF]; the lookup
    // order: -linemodel pwor flips to it, else SF_Stud_act when the world has it.
    pLineActor = nullptr;
    {
        EdenWorld &world = view.getWorld();
        const char *pWanted = "SF_Stud_act";
        if ( const char *pFlag = CmdLine::GetArgValue( "-linemodel" ) ) {
            if ( _stricmp( pFlag, "pwor" ) == 0 ) {
                pWanted = "ss_pwor_a";
            } else if ( _stricmp( pFlag, "cone" ) == 0 ) {
                pWanted = nullptr; // the procedural cone
            }
        }
        if ( pWanted != nullptr ) {
            for ( const auto &entry : world.getActorNames() ) {
                if ( StringCmpI( entry.second.c_str(), pWanted ) != 0 ) {
                    continue;
                }
                EdenModel *pModel = world.getActorModelPtr( entry.first );
                if ( pModel != nullptr && pModel->isLoaded() ) {
                    pLineActor = pModel;
                    lineActorMin = pModel->getBoundsMin();
                    lineActorMax = pModel->getBoundsMax();
                    OTDU_LOG_INFO( "RaceGates: %s: bounds (%.2f, %.2f, %.2f) - (%.2f, %.2f, %.2f)\n", pWanted,
                                   lineActorMin.x, lineActorMin.y, lineActorMin.z, lineActorMax.x, lineActorMax.y,
                                   lineActorMax.z );
                } else {
                    OTDU_LOG_WARN( "RaceGates: %s: actor found (%016llx), model not loaded\n", pWanted,
                                   (unsigned long long)entry.first );
                }
                break;
            }
        }
    }
    // The traffic cone of the cone lines [INF: the game data has no cone model; the retail cones are small orange cones with a
    // white band on a square foot, ~0.6 m tall in the 360 captures]: generated like the pool ring (one index for v / vn / vt).
    // The fallback when the world has no line actor (-linemodel cone forces it).
    UnloadObjDraws( coneDraws );
    if ( pLineActor == nullptr ) {
    {
        std::string obj;
        char line[256];
        constexpr int32_t kSides = 12;
        constexpr float kPi2 = 6.2831853f, kHeight = 0.62f, kBaseR = 0.17f;
        int32_t vertex = 1;
        auto emit = [&]( float x, float y, float z, float nx, float ny, float nz ) {
            snprintf( line, sizeof( line ), "v %.4f %.4f %.4f\nvn %.4f %.4f %.4f\nvt 0 0\n", x, y, z, nx, ny, nz );
            obj += line;
            return vertex++;
        };
        auto tri = [&]( int32_t a, int32_t b, int32_t c ) {
            snprintf( line, sizeof( line ), "f %d/%d/%d %d/%d/%d %d/%d/%d\n", a, a, a, b, b, b, c, c, c );
            obj += line;
        };
        // the foot: a flat square slab (top + four sides)
        obj += "usemtl black\n";
        {
            const float f = 0.20f, y1 = 0.05f;
            const float cx[4] = { -f, f, f, -f }, cz[4] = { -f, -f, f, f };
            int32_t top[4], low[4];
            for ( int32_t i = 0; i < 4; i++ ) {
                top[i] = emit( cx[i], y1, cz[i], 0.0f, 1.0f, 0.0f );
                low[i] = emit( cx[i], 0.0f, cz[i], 0.0f, 1.0f, 0.0f );
            }
            tri( top[0], top[2], top[1] );
            tri( top[0], top[3], top[2] );
            for ( int32_t i = 0; i < 4; i++ ) {
                const int32_t j = ( i + 1 ) % 4;
                tri( low[i], top[j], top[i] );
                tri( low[i], low[j], top[j] );
            }
        }
        struct Band { float y0, y1; const char *pMaterial; };
        const Band bands[] = { { 0.05f, 0.24f, "orange" }, { 0.24f, 0.38f, "white" }, { 0.38f, kHeight, "orange" } };
        for ( const Band &band : bands ) {
            snprintf( line, sizeof( line ), "usemtl %s\n", band.pMaterial );
            obj += line;
            int32_t lo[kSides], hi[kSides];
            for ( int32_t k = 0; k < kSides; k++ ) {
                const float a = kPi2 * static_cast<float>( k ) / static_cast<float>( kSides );
                const float rl = kBaseR * ( 1.0f - band.y0 / kHeight ), rh = kBaseR * ( 1.0f - band.y1 / kHeight );
                lo[k] = emit( rl * std::cos( a ), band.y0, -rl * std::sin( a ), std::cos( a ), 0.35f, -std::sin( a ) );
                hi[k] = emit( rh * std::cos( a ), band.y1, -rh * std::sin( a ), std::cos( a ), 0.35f, -std::sin( a ) );
            }
            for ( int32_t k = 0; k < kSides; k++ ) {
                const int32_t k1 = ( k + 1 ) % kSides;
                tri( lo[k], hi[k1], hi[k] );
                tri( lo[k], lo[k1], hi[k1] );
            }
        }
        const std::string mtl = "newmtl black\nKd 1 1 1\nmap_Kd cone_black.png\nnewmtl orange\nKd 1 1 1\nmap_Kd cone_orange.png\n"
                                "newmtl white\nKd 1 1 1\nmap_Kd cone_white.png\n";
        if ( BuildObjDraws( *pRenderer, "", "trafficcone", "TD_Gouraud", obj, mtl, coneDraws, nullptr, nullptr, nullptr, "RaceGates" ) ) {
            static const uint8_t colors[3][4] = { { 28, 28, 30, 255 }, { 255, 110, 20, 255 }, { 240, 240, 236, 255 } };
            static const char *names[3] = { "cone_black.png", "cone_orange.png", "cone_white.png" };
            for ( size_t d = 0; d < coneDraws.size() && d < 3; d++ ) {
                uint8_t pixels[16];
                for ( int32_t k = 0; k < 4; k++ ) {
                    memcpy( pixels + k * 4, colors[d], 4 );
                }
                if ( GPUTexture *pTexture = gpRender->getTextureCache().createFromRGBA( 2, 2, pixels, names[d] ) ) {
                    pRenderer->assignTexture( coneDraws[d].Material, "COLOR", pTexture );
                    pRenderer->updateProgram( coneDraws[d].Material );
                    coneDraws[d].pTexture = pTexture;
                }
            }
        }
    }
    } // the procedural cone fallback (only when no retail line actor)
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
    pSeparator[0] = pSeparator[1] = nullptr;
    pLineActor = nullptr;
    UnloadObjDraws( coneDraws );
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
        if ( marker.Model == eModel::Cone ) {
            const Matrix4x4f at = Matrix4x4f::Translation( marker.Position );
            for ( const ObjMeshDraw &draw : coneDraws ) {
                renderer.draw( draw.Mesh, draw.Material, at );
            }
            continue;
        }
        if ( marker.Model == eModel::LineActor ) {
            if ( pLineActor != nullptr ) {
                pLineActor->draw( renderer, Matrix4x4f::Translation( marker.Position ) *
                                                Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), marker.Heading ) ) );
            }
            continue;
        }
        if ( marker.Model == eModel::Separator ) {
            if ( pSeparator[marker.Variant & 1] != nullptr ) {
                pSeparator[marker.Variant & 1]->draw( renderer, Matrix4x4f::Translation( marker.Position ) *
                                                Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), marker.Heading ) ) );
            }
            continue;
        }
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
