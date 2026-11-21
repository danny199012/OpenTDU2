#include "shared.h"
#include "spot_exterior.h"
#include "physics/havok_collision.h"

#include "core/xmb.h"
#include "render/gs_render.h"
#include "render/texture_cache.h"

SpotExterior::~SpotExterior() { unload(); }

bool SpotExterior::load( EdenSceneView &view, const std::string &bankPath ) {
    unload();
    path = bankPath;
    setLocal = IsWashBankPath( path ) ? WashSetLocal() : Matrix4x4f::Identity();
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    if ( pRenderer == nullptr || !bank.load( path.c_str() ) ) {
        OTDU_LOG_WARN( "SpotExterior: '%s' could not be loaded\n", path.c_str() );
        return false;
    }
    gpRender->getTextureCache().loadBankTextures( bank, bank.getPath().c_str() );
    hashcodes.load( bank );
    const BankEntry *pEntities = bank.findEntry( "entities.xmb" );
    XmbDocument entities;
    if ( pEntities == nullptr || !entities.load( pEntities->pData, pEntities->Size ) ) {
        OTDU_LOG_WARN( "SpotExterior: '%s' has no entities.xmb\n", path.c_str() );
        return false;
    }
    const Render2DM *pLibrary = &view.getWorld().getLibraryMaterials();
    auto loadModel = [&]( uint64_t rsc ) -> std::unique_ptr<EdenModel> {
        const BankEntry *pScene = rsc != 0 ? hashcodes.find( rsc, 0, 0 ) : nullptr;
        if ( pScene == nullptr ) {
            return nullptr;
        }
        const BankEntry *pGeometry = bank.findEntry( pScene->Name.c_str(), "3dg" );
        const BankEntry *pMaterials = bank.findEntry( pScene->Name.c_str(), "2dm" );
        auto pModel = std::make_unique<EdenModel>();
        pModel->setKeepCollision( path.find( "/ecw_" ) != std::string::npos ); // the car wash floor (collectFloorTriangles)
        if ( path.find( "/ecw_" ) != std::string::npos && CmdLine::HasArg( "-dropwashstrip" ) ) {
            // The forecourt plate is the wash's real floor (y 0 = the road height, a 0.2 m rim): draw it,
            // minus the strip over the road along the front (bank-local z >= 26.3, 6 m) [DATA: the plate
            // mesh is a polygon whose front lip lies over the road band z 24..32].
            pModel->setTriangleDrop( []( const Vector3f &a, const Vector3f &b, const Vector3f &c ) {
                const float lo = Min( a.y, Min( b.y, c.y ) ), hi = Max( a.y, Max( b.y, c.y ) );
                return lo > -0.3f && hi < 0.3f && Min( a.z, Min( b.z, c.z ) ) >= 26.3f;
            } );
        }
        if ( pGeometry == nullptr ||
             !pModel->load( *pRenderer, *pScene, *pGeometry, pMaterials, pLibrary, bank.getPath().c_str() ) ) {
            return nullptr;
        }
        pModel->bindUVAnimations( bank );
        return pModel;
    };
    auto firstMesh = []( XmbValue entry, const char *pField ) -> uint64_t {
        for ( uint32_t j = 0; j < entry.getArraySize( pField ); j++ ) {
            XmbValue mesh = entry.getArrayElement( pField, j );
            if ( mesh["rscSubType"].asInt() == 0 ) {
                return mesh["rsc"].asUInt64();
            }
        }
        return 0;
    };
    XmbValue scenery = entities.getRoot()["ExportType_Scenery"];
    for ( uint32_t i = 0; i < scenery.getArraySize( "ArrayOf_Scenery" ); i++ ) {
        XmbValue entry = scenery.getArrayElement( "ArrayOf_Scenery", i );
        for ( uint32_t j = 0; j < entry.getArraySize( "Model" ); j++ ) {
            XmbValue model = entry.getArrayElement( "Model", j );
            if ( model["rscSubType"].asInt() == 0 ) {
                if ( auto pModel = loadModel( model["rsc"].asUInt64() ) ) {
                    sets.push_back( std::move( pModel ) );
                }
            }
        }
    }
    XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
    for ( const char *pArray : { "ArrayOf_Entity__Actor__Mesh", "ArrayOf_Entity__Actor__Interior" } ) {
        for ( uint32_t i = 0; i < entityExport.getArraySize( pArray ); i++ ) {
            if ( auto pModel = loadModel( firstMesh( entityExport.getArrayElement( pArray, i ), "mesh" ) ) ) {
                sets.push_back( std::move( pModel ) );
            }
        }
    }
    for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__Actor" ); i++ ) {
        XmbValue actor = entityExport.getArrayElement( "ArrayOf_Entity__Actor", i );
        Prop prop;
        prop.Model = loadModel( firstMesh( actor, "mesh" ) );
        if ( !prop.Model ) {
            continue;
        }
        XmbValue placement = actor["orient_pos_scale"];
        const Quaternionf rotation( placement["rot"]["X"].asFloat(), placement["rot"]["Y"].asFloat(),
                                    placement["rot"]["Z"].asFloat(), placement["rot"]["W"].asFloat( 1.0f ) );
        const Vector3f position( placement["pos"]["X"].asFloat(), placement["pos"]["Y"].asFloat(),
                                 placement["pos"]["Z"].asFloat() );
        Matrix4x4f rootInverse;
        for ( const SceneNode &node : prop.Model->getScene().getNodes() ) {
            if ( node.Parent < 0 ) {
                rootInverse = node.Local.inverse();
                break;
            }
        }
        prop.Local = Matrix4x4f::Translation( position ) * EdenRotationFromQuaternion( rotation ) * rootInverse;
        props.push_back( std::move( prop ) );
    }
    registerLightmaps( view );
    // The models' local bounds: the building's footprint in the spot frame
    // T(position) R_y(heading) (the spawn offsets of the launch/*.bat touring
    // starts are read from it).
    Vector3f boundsMin( 0.0f, 0.0f, 0.0f ), boundsMax( 0.0f, 0.0f, 0.0f );
    bool bHasBounds = false;
    for ( const auto &pSet : sets ) {
        if ( !bHasBounds ) {
            boundsMin = pSet->getBoundsMin();
            boundsMax = pSet->getBoundsMax();
            bHasBounds = true;
            continue;
        }
        boundsMin = Vector3f( Min( boundsMin.x, pSet->getBoundsMin().x ), Min( boundsMin.y, pSet->getBoundsMin().y ),
                              Min( boundsMin.z, pSet->getBoundsMin().z ) );
        boundsMax = Vector3f( Max( boundsMax.x, pSet->getBoundsMax().x ), Max( boundsMax.y, pSet->getBoundsMax().y ),
                              Max( boundsMax.z, pSet->getBoundsMax().z ) );
    }
    // The car washes (ecw_a, Ibiza x3 / Hawai x5): the bank's flat forecourt ground plate
    // (item 2, 42 x 56 m, 0.4 m thick) is drawn over the road that runs along the wash front
    // at bank-local z 24..28 and hides it (the owner, 2026-11-15: "it's blocking the road";
    // the collision never blocked, measured with -scanwash). The plate is not drawn; the
    // world's own terrain / road shows. -keepwashplate keeps it. [INF: retail drew the road
    // differently there; the data places the plate over the link].
    if ( path.find( "/ecw_" ) != std::string::npos && CmdLine::HasArg( "-hidewashplate" ) ) {
        for ( auto &pSet : sets ) {
            const auto &setItems = pSet->getItems();
            for ( size_t i = 0; i < setItems.size(); i++ ) {
                const Vector3f ext = setItems[i].BoundsMax - setItems[i].BoundsMin;
                if ( ext.y < 0.5f && ext.x > 40.0f && ext.z > 50.0f ) {
                    if ( CmdLine::HasArg( "-logwashplate" ) ) {
                        const auto &cv = pSet->getCollisionVertices();
                        const auto &ci = pSet->getCollisionIndices();
                        for ( uint32_t k = setItems[i].CollisionFirst; k + 2 < setItems[i].CollisionFirst + setItems[i].CollisionCount; k += 3 ) {
                            const Vector3f &a = cv[ci[k]], &b = cv[ci[k + 1]], &c = cv[ci[k + 2]];
                            OTDU_LOG_INFO( "washplate tri (%.1f %.1f %.1f) (%.1f %.1f %.1f) (%.1f %.1f %.1f)\n", a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z );
                        }
                    }
                    pSet->setItemHidden( i, true );
                }
            }
        }
    }
    renderBoundsMin = boundsMin;
    renderBoundsMax = boundsMax;
    if ( CmdLine::HasArg( "-logspotitems" ) ) {
        for ( const auto &pSet : sets ) {
            for ( const EdenModel::Item &item : pSet->getItems() ) {
                const SceneNode &node = pSet->getScene().getNodes()[item.NodeIndex];
                const Matrix4x4f &w = pSet->getNodeTransform( item.NodeIndex );
                const Vector3f lo = w.transformPoint( item.BoundsMin ), hi = w.transformPoint( item.BoundsMax );
                OTDU_LOG_INFO( "spotitem %s: node %d '%s' mat %u lod %u bounds (%.1f %.1f %.1f)-(%.1f %.1f %.1f) verts %u\n",
                               path.c_str(), item.NodeIndex, node.Name.c_str(), item.MaterialIndex, item.Lod, lo.x, lo.y,
                               lo.z, hi.x, hi.y, hi.z, item.NumVertices );
            }
        }
    }
    OTDU_LOG_INFO( "SpotExterior: %s: %zu set models, %zu props, local bounds (%.1f, %.1f, %.1f) - (%.1f, %.1f, %.1f)\n",
                   path.c_str(), sets.size(), props.size(), boundsMin.x, boundsMin.y, boundsMin.z, boundsMax.x,
                   boundsMax.y, boundsMax.z );
    return true;
}

bool IsWashBankPath( const std::string &path ) {
    return path.find( "/ecw_" ) != std::string::npos;
}

Matrix4x4f WashSetLocal() {
    static const bool bOff = CmdLine::HasArg( "-nowashrot" );
    if ( bOff ) {
        return Matrix4x4f::Identity();
    }
    return Matrix4x4f::Translation( Vector3f( -1.5f, 0.0f, -16.0f ) );
}

void SpotExterior::collectFloorTriangles( float minY, float maxY, HavokCollision &out ) const {
    if ( CmdLine::HasArg( "-dumpwashverts" ) ) {
        if ( FILE *pFile = fopen( "_wash_verts.txt", "w" ) ) {
            for ( const auto &pSet : sets ) {
                for ( const Vector3f &v : pSet->getCollisionVertices() ) {
                    fprintf( pFile, "%.3f %.3f %.3f\n", v.x, v.y, v.z );
                }
            }
            fclose( pFile );
        }
    }
    for ( const auto &pSet : sets ) {
        const std::vector<Vector3f> &vertices = pSet->getCollisionVertices();
        const std::vector<uint32_t> &indices = pSet->getCollisionIndices();
        for ( size_t i = 0; i + 2 < indices.size(); i += 3 ) {
            const Vector3f &a = vertices[indices[i]], &b = vertices[indices[i + 1]], &c = vertices[indices[i + 2]];
            if ( a.y < minY || a.y > maxY || b.y < minY || b.y > maxY || c.y < minY || c.y > maxY ) {
                continue;
            }
            const Vector3f normal = ( b - a ).cross( c - a );
            if ( normal.length() < 1e-4f || fabsf( normal.y ) < 0.9f * normal.length() ) {
                continue;
            }
            const uint32_t base = static_cast<uint32_t>( out.Vertices.size() );
            out.Vertices.push_back( a );
            out.Vertices.push_back( b );
            out.Vertices.push_back( c );
            out.Indices.push_back( base );
            out.Indices.push_back( base + 1 );
            out.Indices.push_back( base + 2 );
            out.TriMaterial.push_back( 0 );
        }
    }
    if ( !out.Indices.empty() && out.Bodies.empty() ) {
        HavokCollision::Body body;
        body.Transform = Matrix4x4f::Identity();
        body.MotionType = 7;
        body.Friction = 0.8f;
        out.Bodies.push_back( body );
    }
    if ( !out.Bodies.empty() ) {
        out.Bodies[0].FirstTriangle = 0;
        out.Bodies[0].NumTriangles = static_cast<uint32_t>( out.Indices.size() / 3 );
    }
}

void SpotExterior::registerLightmaps( EdenSceneView &view ) {
    // The session-45 no-op is back (FUN_00981870 / FUN_00980d30, the night
    // law now decoded: docs/re/night_lighting_spec.md 3): NIGHTLMAP
    // registration again, plus the OcclusionDayNight sprites and the TD_Lamp
    // materials (their glow switches with the weather) and the neons. The
    // session-45 daylight regression is kept out differently: the exterior
    // NIGHTLMAP overwrite only applies when the weather's blend points at
    // the night lightmap (updateLightmapMaterials, y > 0), so the authored
    // day look (the Ferrari dealer showroom's (0.44, 1.0)) is untouched.
    for ( auto &pSet : sets ) {
        view.registerLightmapMaterials( *pSet, EdenSceneView::eLightmapScope::Exterior );
        view.registerNeonMaterials( *pSet );
    }
    for ( Prop &prop : props ) {
        view.registerLightmapMaterials( *prop.Model, EdenSceneView::eLightmapScope::Exterior );
        view.registerNeonMaterials( *prop.Model );
    }
}

void SpotExterior::unload() {
    sets.clear();
    props.clear();
    if ( !bank.getEntries().empty() && gpRender != nullptr ) {
        gpRender->getTextureCache().releaseOwner( bank.getPath().c_str() );
    }
    bank.unload();
}

void SpotExterior::draw( EdenRenderer &renderer ) const {
    for ( const Matrix4x4f &frame : frames ) {
        for ( const auto &pSet : sets ) {
            pSet->draw( renderer, frame * setLocal );
        }
        for ( const Prop &prop : props ) {
            prop.Model->draw( renderer, frame * prop.Local );
        }
    }
}

void SpotExterior::applyVisibilityOverrides( const std::unordered_set<uint64_t> &hiddenNodes,
                                             const std::unordered_set<uint64_t> &hiddenMaterials,
                                             const std::vector<std::string> &hiddenMaterialNames,
                                             const std::unordered_map<uint64_t, Vector4f> &colorHashes,
                                             const std::vector<std::pair<std::string, Vector4f>> &colorNames ) const {
    for ( const auto &pSet : sets ) {
        EdenModelApplyVisibility( *pSet, hiddenNodes, hiddenMaterials, hiddenMaterialNames );
        EdenModelApplyMaterialColors( *pSet, colorHashes, colorNames );
    }
    for ( const Prop &prop : props ) {
        EdenModelApplyVisibility( *prop.Model, hiddenNodes, hiddenMaterials, hiddenMaterialNames );
        EdenModelApplyMaterialColors( *prop.Model, colorHashes, colorNames );
    }
}
