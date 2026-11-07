#include "shared.h"
#include "eden_planar_reflections.h"

#include "render/eden/eden_model.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"

namespace {
constexpr uint64_t kTD_Water = 0x0123000000000000ull;
constexpr uint64_t kTD_Furnitures = 0xd5013ed0db79afafull;
constexpr uint64_t kRuntimeSlot = ~0ull; // slot hash -1: runtime texture
constexpr uint8_t kSamplerLinearClamp = 1; // EdenRenderer sampler heap SH_LinearClamp

// 0x00f817c0 { guid, spec byte (U<n>: switch 0x1f + n), slot }
struct MaterialRule {
    uint64_t Guid;
    uint8_t SpecIndex;
    uint8_t Slot;
};
constexpr MaterialRule kMaterialRules[] = {
    { kTD_Furnitures, 0x1f + 7, 6 },
    { kTD_Water, 0x1f + 4, 4 },
};

// 0x00f817e0 { original game mode id, enable }, -1 terminated [CODE].
constexpr int32_t kModeTable[][2] = {
    { 0x00, 0 }, { 0x01, 0 }, { 0x02, 0 }, { 0x03, 1 }, { 0x04, 0 }, { 0x05, 0 }, { 0x06, 1 }, { 0x07, 0 },
    { 0x08, 1 }, { 0x09, 1 }, { 0x0b, 1 }, { 0x2a, 1 }, { 0x0c, 1 }, { 0x0d, 0 }, { 0x0e, 0 }, { 0x0f, 0 },
    { 0x10, 1 }, { 0x11, 1 }, { 0x12, 1 }, { 0x13, 1 }, { 0x14, 0 }, { 0x15, 0 }, { 0x16, 1 }, { 0x17, 1 },
    { 0x18, 0 }, { 0x19, 1 }, { 0x1a, 1 }, { 0x1b, 1 }, { 0x1c, 0 }, { 0x1d, 1 }, { 0x1e, 1 }, { 0x1f, 1 },
    { 0x20, 1 }, { 0x21, 1 }, { 0x22, 1 }, { 0x24, 1 }, { 0x0a, 1 }, { 0x1f, 1 }, { 0x23, 1 },
};

bool ContainsNoCase( const char *pText, const char *pPattern ) {
    if ( pText == nullptr ) {
        return false;
    }
    return ToUpper( std::string( pText ) ).find( pPattern ) != std::string::npos;
}
} // namespace

bool EdenPlanarReflections::IsEnabledForGameMode( int32_t originalModeId ) {
    for ( const auto &entry : kModeTable ) {
        if ( entry[0] == originalModeId ) {
            return entry[1] != 0;
        }
    }
    return false;
}

void EdenPlanarReflections::destroy() {
    clear();
    if ( pTarget != nullptr && gpRender != nullptr ) {
        gpRender->getDevice().destroyRenderTarget( pTarget );
    }
    pTarget = nullptr;
}

void EdenPlanarReflections::clear() {
    projectors.clear();
    materials.clear();
}

void EdenPlanarReflections::applyMaterial( EdenRenderer &renderer, const RegisteredMaterial &entry, bool bEnable ) {
    // FUN_00d7de80(enable): spec[specByte] = enable, slot hash = -1 / 0,
    // sampler address bytes +0x116 / +0x117 = 2 (clamp [INF]), shader
    // selected again (FUN_008a8950).
    EdenMaterial &material = *entry.pMaterial;
    material.Spec[entry.SpecIndex] = bEnable ? 1 : 0;
    if ( entry.Slot >= material.NumSlots ) {
        material.NumSlots = entry.Slot + 1u;
    }
    material.TextureHashes[entry.Slot] = bEnable ? kRuntimeSlot : 0ull;
    material.pTextures[entry.Slot] = nullptr;
    material.SamplerIndex[entry.Slot] = kSamplerLinearClamp;
    material.ProjectiveSlot = bEnable ? static_cast<int8_t>( entry.Slot ) : -1;
    renderer.updateProgram( material );
}

void EdenPlanarReflections::setEnabled( EdenRenderer &renderer, bool bEnable ) {
    if ( bEnable == bEnabled ) {
        return;
    }
    bEnabled = bEnable;
    for ( const RegisteredMaterial &entry : materials ) {
        applyMaterial( renderer, entry, bEnabled );
    }
}

uint32_t EdenPlanarReflections::registerMaterials( EdenRenderer &renderer, EdenModel &model ) {
    uint32_t numRegistered = 0;
    for ( EdenMaterial &material : model.getMaterials() ) {
        if ( materials.size() >= kMaxMaterials ) {
            break;
        }
        bool bKnown = false;
        for ( const RegisteredMaterial &entry : materials ) {
            bKnown |= entry.pMaterial == &material;
        }
        if ( bKnown ) {
            continue;
        }
        for ( const MaterialRule &rule : kMaterialRules ) {
            if ( material.ShaderSetGUID != rule.Guid || material.Spec[rule.SpecIndex] != 1 ) {
                continue;
            }
            RegisteredMaterial entry;
            entry.pMaterial = &material;
            entry.SpecIndex = rule.SpecIndex;
            entry.Slot = rule.Slot;
            materials.push_back( entry );
            // TD_Water: the original binds SKYSPHEREMAP (a 64x64 2D render
            // target) to slot 3, which every TD_Water pixel shader samples as
            // a cube: undefined under D3D9. The bank cube map is kept [INF].
            // Disabled: U4 = 0, slot 4 empty (FUN_00d7df50), FUN_00d7de80(0)
            // for TD_Furnitures [INF].
            applyMaterial( renderer, entry, bEnabled );
            numRegistered++;
            OTDU_LOG_DEBUG( "GSReflection: material '%s' slot %u (%s)\n", material.Name.c_str(), rule.Slot,
                            bEnabled ? "on" : "off" );
            break;
        }
    }
    return numRegistered;
}

uint32_t EdenPlanarReflections::registerScene( const EdenModel &model, const Matrix4x4f &world, const char *pBankName,
                                               const char *pActorLayer1, const char *pActorLayer2 ) {
    // "EPH" / "ECAS" banks (house exteriors, casino): no actor layer
    // (strstr on the bank name; case [INF]).
    const bool bExteriorBank = ContainsNoCase( pBankName, "EPH" ) || ContainsNoCase( pBankName, "ECAS" );
    const uint64_t layer1 = pActorLayer1 != nullptr && pActorLayer1[0] != '\0' ? GetIdentifier64bit( pActorLayer1 ) : 0;
    const uint64_t layer2 = pActorLayer2 != nullptr && pActorLayer2[0] != '\0' ? GetIdentifier64bit( pActorLayer2 ) : 0;
    uint32_t numActorLayers = 0;
    for ( const Projector &projector : projectors ) {
        numActorLayers += projector.bActorLayer ? 1 : 0;
    }

    const std::vector<SceneNode> &nodes = model.getScene().getNodes();
    uint32_t numAdded = 0;
    Projector *pCurrent = nullptr;
    OTDU_LOG_DEBUG( "GSReflection: '%s' layers '%s' (%016llx) / '%s' (%016llx)\n", pBankName,
                    pActorLayer1 != nullptr ? pActorLayer1 : "",
                    layer1, pActorLayer2 != nullptr ? pActorLayer2 : "", layer2 );
    for ( int32_t nodeIndex = 0; nodeIndex < static_cast<int32_t>( nodes.size() ); nodeIndex++ ) {
        const SceneNode &node = nodes[nodeIndex];
        Vector3f boundsMin, boundsMax;
        if ( model.getNodeBounds( nodeIndex, boundsMin, boundsMax ) ) {
            // FUN_00dd63e0: object of the current projector.
            if ( pCurrent != nullptr && pCurrent->Nodes.size() < kMaxObjects ) {
                pCurrent->Nodes.push_back( nodeIndex );
            }
            continue;
        }
        if ( node.ObjectHashcode != 0 ) {
            continue; // geometry that was not loaded
        }
        if ( projectors.size() >= kMaxProjectors ) {
            pCurrent = nullptr;
            continue;
        }
        Projector projector;
        projector.Name = node.Name;
        projector.pModel = &model;
        projector.World = world;
        // n = node Y axis, d = n . node position [INF: node +0x20 / +0x40].
        const Matrix4x4f nodeWorld = world * model.getNodeTransform( nodeIndex );
        projector.Normal = nodeWorld.transformVector( Vector3f( 0.0f, 1.0f, 0.0f ) ).normalized();
        projector.Distance = projector.Normal.dot( nodeWorld.getTranslation() );
        if ( node.Name.find( "REFRACT" ) != std::string::npos ) {
            // FUN_00dd6070: plane, identity matrix, refraction.
            projector.bRefraction = true;
            projector.Mirror = Matrix4x4f::Identity();
        } else {
            // FUN_00dd60c0: mirror matrix (D3D layout, row vectors).
            const Vector3f &n = projector.Normal;
            const float nv[3] = { n.x, n.y, n.z };
            Matrix4x4f mirror = Matrix4x4f::Identity();
            for ( int i = 0; i < 3; i++ ) {
                for ( int j = 0; j < 3; j++ ) {
                    mirror.m[i][j] = ( i == j ? 1.0f : 0.0f ) - 2.0f * nv[i] * nv[j];
                }
                mirror.m[3][i] = 2.0f * projector.Distance * nv[i];
            }
            projector.Mirror = mirror;
            if ( !bExteriorBank && numActorLayers < kMaxActorLayers &&
                 ( ( layer1 != 0 && node.Hashcode == layer1 ) || ( layer2 != 0 && node.Hashcode == layer2 ) ) ) {
                projector.bActorLayer = true;
                numActorLayers++;
            }
        }
        OTDU_LOG_INFO( "GSReflection: projector '%s' %s n (%.3f %.3f %.3f) d %.3f\n", projector.Name.c_str(),
                       projector.bRefraction ? "refraction" : ( projector.bActorLayer ? "mirror (actors)" : "mirror" ),
                       projector.Normal.x, projector.Normal.y, projector.Normal.z, projector.Distance );
        projectors.push_back( std::move( projector ) );
        pCurrent = &projectors.back();
        numAdded++;
    }
    for ( const Projector &p : projectors ) {
        OTDU_LOG_INFO( "GSReflection:   '%s': %zu object nodes\n", p.Name.c_str(), p.Nodes.size() );
    }
    return numAdded;
}

bool EdenPlanarReflections::isActive() const {
    static const bool bDisabled = CmdLine::HasArg( "-noprojectors" );
    return bEnabled && !bDisabled && !materials.empty() && !projectors.empty();
}

GPUTexture *EdenPlanarReflections::getTexture() const {
    return pTarget != nullptr && pTarget->bInitialized ? &pTarget->Color : nullptr;
}

void EdenPlanarReflections::ensureTarget( uint32_t width, uint32_t height ) {
    RenderDevice &device = gpRender->getDevice();
    if ( pTarget != nullptr && pTarget->Width == width && pTarget->Height == height ) {
        return;
    }
    device.destroyRenderTarget( pTarget );
    pTarget = device.createRenderTarget( width, height, VK_FORMAT_R8G8B8A8_UNORM, true, "GSReflection" );
}

namespace {
// Lengyel oblique near plane: clips everything on the wrong side of the
// world plane (normal, distance) by warping the projection (the same trick
// the ocean reflection uses; the original used a user clip plane).
void ClipToPlane( EdenSceneParams &params, const Vector3f &normal, float distance ) {
    const Matrix4x4f inverseTranspose = params.View.inverse().transposed();
    const Vector4f planeWorld( normal.x, normal.y, normal.z, -distance );
    Vector4f plane( 0.0f, 0.0f, 0.0f, 0.0f );
    // plane_view = inverseTranspose * planeWorld (the View row-vector
    // convention stores m[column][row]).
    plane.x = inverseTranspose.m[0][0] * planeWorld.x + inverseTranspose.m[1][0] * planeWorld.y +
              inverseTranspose.m[2][0] * planeWorld.z + inverseTranspose.m[3][0] * planeWorld.w;
    plane.y = inverseTranspose.m[0][1] * planeWorld.x + inverseTranspose.m[1][1] * planeWorld.y +
              inverseTranspose.m[2][1] * planeWorld.z + inverseTranspose.m[3][1] * planeWorld.w;
    plane.z = inverseTranspose.m[0][2] * planeWorld.x + inverseTranspose.m[1][2] * planeWorld.y +
              inverseTranspose.m[2][2] * planeWorld.z + inverseTranspose.m[3][2] * planeWorld.w;
    plane.w = inverseTranspose.m[0][3] * planeWorld.x + inverseTranspose.m[1][3] * planeWorld.y +
              inverseTranspose.m[2][3] * planeWorld.z + inverseTranspose.m[3][3] * planeWorld.w;
    const Matrix4x4f inverseProjection = params.Projection.inverse();
    const float sx = plane.x > 0.0f ? 1.0f : ( plane.x < 0.0f ? -1.0f : 0.0f );
    const float sy = plane.y > 0.0f ? 1.0f : ( plane.y < 0.0f ? -1.0f : 0.0f );
    const Vector4f corner( sx, sy, 1.0f, 1.0f );
    Vector4f q( 0.0f, 0.0f, 0.0f, 0.0f );
    q.x = inverseProjection.m[0][0] * corner.x + inverseProjection.m[1][0] * corner.y +
          inverseProjection.m[2][0] * corner.z + inverseProjection.m[3][0] * corner.w;
    q.y = inverseProjection.m[0][1] * corner.x + inverseProjection.m[1][1] * corner.y +
          inverseProjection.m[2][1] * corner.z + inverseProjection.m[3][1] * corner.w;
    q.z = inverseProjection.m[0][2] * corner.x + inverseProjection.m[1][2] * corner.y +
          inverseProjection.m[2][2] * corner.z + inverseProjection.m[3][2] * corner.w;
    q.w = inverseProjection.m[0][3] * corner.x + inverseProjection.m[1][3] * corner.y +
          inverseProjection.m[2][3] * corner.z + inverseProjection.m[3][3] * corner.w;
    const float dot = plane.x * q.x + plane.y * q.y + plane.z * q.z + plane.w * q.w;
    if ( std::abs( dot ) > 1e-8f && plane.w < 0.0f ) {
        const float scale = 1.0f / dot;
        params.Projection.m[0][2] = plane.x * scale;
        params.Projection.m[1][2] = plane.y * scale;
        params.Projection.m[2][2] = plane.z * scale;
        params.Projection.m[3][2] = plane.w * scale;
    }
}
} // namespace

void EdenPlanarReflections::render( EdenRenderer &renderer, const EdenSceneParams &mainScene,
                                    const DrawActors &drawActors ) {
    renderer.setProjectiveTexture( nullptr );
    if ( !isActive() ) {
        return;
    }
    RenderDevice &device = gpRender->getDevice();
    ensureTarget( mainScene.Width, mainScene.Height );
    if ( pTarget == nullptr ) {
        return;
    }
    device.beginRenderTarget( pTarget, true, ColorRGBA( 0.0f, 0.0f, 0.0f, 0.0f ) );

    // REFLECT FOR ACTORS 1ST / 2ND LAYER: the actors seen by the mirror
    // camera (FUN_00dd6300) — the mirror images of the TD_Furnitures
    // reflective floors. Drawn first: every layer renders into the same
    // screen-sized RT (only the depth is cleared between them), and the
    // TD_Water refraction content must stay on top — the water shader
    // samples the whole RT as its refraction (85 % weight, alpha = valid
    // underwater pixel), so mirror content that covers the pool on screen
    // would otherwise show up as a distorted smear in the water.
    // The render is clipped to the mirror plane: only what stands on the
    // reflecting side is mirrored. Without the clip, everything below the
    // plane — for the living-room floor (y = 37.49) that is the whole
    // outdoors, pool and terrain included — is mirrored to the wrong side
    // and lands over the rest of the screen as giant displaced blobs.
    for ( const Projector &projector : projectors ) {
        if ( !projector.bActorLayer || !drawActors ) {
            continue;
        }
        device.clearRenderTargetDepth();
        EdenSceneParams params = mainScene;
        params.View = mainScene.View * projector.Mirror;
        params.CameraPosition = projector.Mirror.transformPoint( mainScene.CameraPosition );
        params.CameraDirection = projector.Mirror.transformVector( mainScene.CameraDirection );
        params.bMirroredView = !mainScene.bMirroredView;
        ClipToPlane( params, projector.Normal, projector.Distance );
        renderer.beginScene( params );
        drawActors( renderer, projector );
        renderer.endScene();
    }

    // REFLECT 2D: objects of the refraction projectors and of the mirrors
    // without an actor layer, main camera (own depth clear: the mirror
    // passes leave their depth behind).
    device.clearRenderTargetDepth();
    renderer.beginScene( mainScene );
    bool bActorsDrawn = false;
    for ( const Projector &projector : projectors ) {
        if ( projector.bActorLayer ) {
            continue;
        }
        for ( int32_t node : projector.Nodes ) {
            projector.pModel->drawNodeItems( renderer, node, projector.World * projector.pModel->getNodeTransform( node ) );
        }
        if ( projector.bRefraction && bRefractionActors && !bActorsDrawn && drawActors ) {
            drawActors( renderer, projector );
            bActorsDrawn = true;
        }
    }
    renderer.endScene();
    device.endRenderTarget();
    renderer.setProjectiveTexture( getTexture() );
}

void EdenPlanarReflections::unbind( EdenRenderer &renderer ) { renderer.setProjectiveTexture( nullptr ); }
