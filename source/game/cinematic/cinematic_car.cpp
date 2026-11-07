#include "shared.h"
#include "cinematic_car.h"

#include "core/xmb.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "render/gs_render.h"
#include "render/eden/eden_weather.h"
#include "render/eden/eden_scene_view.h"
#include "system/cmd_line.h"

namespace {
// Scalar lerp of the paint / dirt blends (the chase camera's local helper has
// internal linkage too; this TU does not include it).
inline float Lerp1( float a, float b, float t ) {
    return a + ( b - a ) * t;
}

// Window animation: the WND_x1 glass slides onto the WND_x2 rest over this
// long [INF: the retail factor function FUN_00c66150(f) has no readable
// duration; the hardtop's own clip is 17 s for a much larger move].
constexpr float kWndAnimDuration = 1.2f;
// Blinker cadence (FUN_00c57ef0): t = fmod(t + dt, 1.0), lamps lit while
// t < 0.6 (QWORD 0x00f44c70 = 1.0 s period, threshold 0x00ef2754).
constexpr float kBlinkerPeriod = 1.0f;
constexpr float kBlinkerOnTime = 0.6f;
// The dashboard sat-nav screen shader (TD_GPS; docs/re/opentdu1_reuse.md 1).
constexpr uint64_t kGpsShaderGuid = 0x7dad16b47eb57b95ull;

// The light material names (table 0x00f7e700, docs/re/car_lights_spec.md 2.1)
// with their side / end. The driver writes GLOSSFCT on each of them.
struct LightMaterialName {
    const char *pName;
    bool bLeft;
    bool bFront;
};
constexpr LightMaterialName kLightMaterials[] = {
    { "LGT_FL", true, true },   { "LGT_FR", false, true },  { "LGT_RL", true, false },
    { "LGT_RR", false, false }, { "LGT_RL_SK", true, false }, { "LGT_RR_SK", false, false },
    { "LGT_L", true, false },   { "LGT_R", false, false },  { "LGT_R_S", false, false },
};

// Dashboard backlight colour when the headlights are on, and the
// HeadlightsIntensity the TD_DashBoard shader multiplies it by.
constexpr Vector4f kDashLitColor( 1.0f, 0.72f, 0.36f, 1.0f );
constexpr float kDashLitIntensity = 1.0f;

// 8 byte scene node name (character i added into byte i & 7, little endian):
// the 3DD instance hashcode of names longer than 8 characters.
uint64_t NodeNameHash( const char *pName ) {
    uint8_t bytes[8] = {};
    for ( size_t i = 0; pName[i] != '\0'; i++ ) {
        bytes[i & 7] = static_cast<uint8_t>( bytes[i & 7] + static_cast<uint8_t>( pName[i] ) );
    }
    uint64_t hash = 0;
    for ( int i = 7; i >= 0; i-- ) {
        hash = ( hash << 8 ) | bytes[i];
    }
    return hash;
}

// --- Plus-size rims (the tuning shop's rim size) --------------------------
// The rim mesh is normalized (outer radius 0.212 m) and drawn scaled to
// (diameter / 2 + the 2.17 cm lip) / 0.212 (the California bakes 1.24 for its
// 19" rims). The tire meshes of common_car.bnk are one per rim diameter
// (T_13P .. T_24P: bead on the rim lip, tread at a canonical radius for that
// diameter). A tuning shop rim size keeps the car's rolling radius: every
// base tire mesh is remapped so its bead sits on the chosen rim's lip and its
// tread on the car's stock tire radius, i.e. the sidewall gets thinner as the
// rim grows. Radial map r -> a r + b (the X axle is unchanged), normals by
// the inverse transpose (n_yz / a).
constexpr float kRimMeshRadius = 0.212f; // normalized rim mesh radius [DATA]
constexpr float kRimLip = 0.0217f;       // flange above the nominal radius [INF]
constexpr int32_t kMinTireDiameterInch = 13;
constexpr int32_t kMaxTireDiameterInch = 24;

float HalfToFloat( uint16_t h ) {
    const uint32_t sign = ( h >> 15 ) & 1;
    int32_t exponent = ( h >> 10 ) & 0x1f;
    uint32_t mantissa = h & 0x3ff;
    uint32_t bits;
    if ( exponent == 0 ) {
        if ( mantissa == 0 ) {
            bits = sign << 31;
        } else {
            exponent = 1;
            while ( !( mantissa & 0x400 ) ) {
                mantissa <<= 1;
                exponent--;
            }
            mantissa &= 0x3ff;
            bits = ( sign << 31 ) | ( static_cast<uint32_t>( exponent + 112 ) << 23 ) | ( mantissa << 13 );
        }
    } else if ( exponent == 31 ) {
        bits = ( sign << 31 ) | 0x7f800000 | ( mantissa << 13 );
    } else {
        bits = ( sign << 31 ) | ( static_cast<uint32_t>( exponent + 112 ) << 23 ) | ( mantissa << 13 );
    }
    float result;
    memcpy( &result, &bits, sizeof( float ) );
    return result;
}

uint16_t FloatToHalf( float value ) {
    uint32_t bits;
    memcpy( &bits, &value, sizeof( bits ) );
    const uint32_t sign = ( bits >> 16 ) & 0x8000;
    int32_t exponent = static_cast<int32_t>( ( bits >> 23 ) & 0xff ) - 127 + 15;
    const uint32_t mantissa = bits & 0x7fffff;
    if ( exponent <= 0 ) {
        if ( exponent < -10 ) {
            return static_cast<uint16_t>( sign );
        }
        return static_cast<uint16_t>( sign | ( ( ( mantissa | 0x800000 ) >> ( 1 - exponent ) ) >> 13 ) );
    }
    if ( exponent >= 31 ) {
        return static_cast<uint16_t>( sign | 0x7c00 );
    }
    return static_cast<uint16_t>( sign | ( static_cast<uint32_t>( exponent ) << 10 ) | ( mantissa >> 13 ) );
}

// Remaps the radial coordinates of one tire mesh (the X axle is unchanged).
void RemapTireGeometry( Geometry &geometry, float beadRadius, float treadRadius ) {
    if ( treadRadius <= beadRadius ) {
        return;
    }
    float nativeBead = 1e30f, nativeTread = -1e30f;
    for ( const GeometryPrimitive &primitive : geometry.Primitives ) {
        for ( const MeshVertex &vertex : primitive.Vertices ) {
            const float radial =
                std::sqrt( vertex.Position[1] * vertex.Position[1] + vertex.Position[2] * vertex.Position[2] );
            nativeBead = Min( nativeBead, radial );
            nativeTread = Max( nativeTread, radial );
        }
    }
    if ( nativeTread - nativeBead < 0.005f ) {
        return;
    }
    const float a = ( treadRadius - beadRadius ) / ( nativeTread - nativeBead );
    const float b = beadRadius - a * nativeBead;
    OTDU_LOG_DEBUG( "CinematicCar: tire mesh remapped: bead %.3f -> %.3f, tread %.3f -> %.3f (a %.3f b %.3f)\n",
                    nativeBead, beadRadius, nativeTread, treadRadius, a, b );
    for ( GeometryPrimitive &primitive : geometry.Primitives ) {
        const RawVertexAttribute *pPosition = nullptr;
        const RawVertexAttribute *pNormal = nullptr;
        for ( const RawVertexAttribute &attribute : primitive.RawAttributes ) {
            if ( attribute.Usage == 0 && attribute.UsageIndex == 0 ) {
                pPosition = &attribute;
            } else if ( attribute.Usage == 3 && attribute.UsageIndex == 0 ) {
                pNormal = &attribute;
            }
        }
        if ( pPosition == nullptr || pPosition->Format != VAF_POSITION_R32G32B32_SFLOAT || pPosition->Stream >= 2 ||
             primitive.RawStrides[pPosition->Stream] == 0 ) {
            continue;
        }
        uint8_t *pPositions = primitive.RawStreams[pPosition->Stream].data();
        const uint32_t stride = primitive.RawStrides[pPosition->Stream];
        const uint32_t count =
            Min<uint32_t>( primitive.NumRawVertex, static_cast<uint32_t>( primitive.Vertices.size() ) );
        for ( uint32_t v = 0; v < count; v++ ) {
            float *p = reinterpret_cast<float *>( pPositions + v * stride + pPosition->Offset );
            const float radial = std::sqrt( p[1] * p[1] + p[2] * p[2] );
            const float scale = radial > 1e-6f ? a + b / radial : a;
            p[1] *= scale;
            p[2] *= scale;
            primitive.Vertices[v].Position[1] *= scale;
            primitive.Vertices[v].Position[2] *= scale;
        }
        if ( pNormal == nullptr || pNormal->Stream >= 2 || primitive.RawStrides[pNormal->Stream] == 0 ||
             ( pNormal->Format != VAF_NORMAL_R32G32B32_SFLOAT &&
               pNormal->Format != VAF_NORMAL_R16G16B16A16_SFLOAT ) ) {
            continue;
        }
        uint8_t *pNormals = primitive.RawStreams[pNormal->Stream].data();
        const uint32_t normalStride = primitive.RawStrides[pNormal->Stream];
        for ( uint32_t v = 0; v < count; v++ ) {
            uint8_t *p = pNormals + v * normalStride + pNormal->Offset;
            float normal[3];
            if ( pNormal->Format == VAF_NORMAL_R32G32B32_SFLOAT ) {
                memcpy( normal, p, sizeof( normal ) );
            } else {
                const uint16_t *pHalf = reinterpret_cast<const uint16_t *>( p );
                normal[0] = HalfToFloat( pHalf[0] );
                normal[1] = HalfToFloat( pHalf[1] );
                normal[2] = HalfToFloat( pHalf[2] );
            }
            normal[1] /= a;
            normal[2] /= a;
            const float length = std::sqrt( normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2] );
            if ( length > 1e-6f ) {
                normal[0] /= length;
                normal[1] /= length;
                normal[2] /= length;
            }
            if ( pNormal->Format == VAF_NORMAL_R32G32B32_SFLOAT ) {
                memcpy( p, normal, sizeof( normal ) );
            } else {
                uint16_t *pHalf = reinterpret_cast<uint16_t *>( p );
                pHalf[0] = FloatToHalf( normal[0] );
                pHalf[1] = FloatToHalf( normal[1] );
                pHalf[2] = FloatToHalf( normal[2] );
            }
            primitive.Vertices[v].Normal[0] = normal[0];
            primitive.Vertices[v].Normal[1] = normal[1];
            primitive.Vertices[v].Normal[2] = normal[2];
        }
    }
}

// The GeometrySetup hook of the tire model: remap every base T_<d>P mesh to
// the plus-size profile of its diameter.
void RemapTireProfiles( Render3DG &geometry, float tireRadius ) {
    if ( tireRadius <= 0.0f ) {
        return;
    }
    for ( int32_t diameter = kMinTireDiameterInch; diameter <= kMaxTireDiameterInch; diameter++ ) {
        const uint64_t hash = NodeNameHash( ( "T_" + std::to_string( diameter ) + "P" ).c_str() );
        auto it = geometry.getGeometries().find( hash );
        if ( it == geometry.getGeometries().end() ) {
            continue;
        }
        RemapTireGeometry( it->second, diameter * 0.0254f * 0.5f + kRimLip, tireRadius );
    }
}

int32_t FindNodeByHash( const Render3DD &scene, const char *pName ) {
    const uint64_t hash = NodeNameHash( pName );
    const std::vector<SceneNode> &nodes = scene.getNodes();
    for ( size_t i = 0; i < nodes.size(); i++ ) {
        if ( nodes[i].Hashcode == hash ) {
            return static_cast<int32_t>( i );
        }
    }
    return -1;
}

// DBCarPhysicsData._car_body of the car bank (the runtime car record +0x8c:
// 0x470 byte records, _id_car at +0x21c).
int32_t FindCarBody( const std::string &fileName ) {
    if ( gpDatabase == nullptr ) {
        return -1;
    }
    XmbValue root = gpDatabase->getDocument().getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
        XmbValue car = root.getArrayElement( "DBCarPhysicsData", i );
        if ( StringCmpI( car["_car_file_name"].asString().c_str(), fileName.c_str() ) == 0 ) {
            return static_cast<int32_t>( car["_car_body"].asInt( -1 ) );
        }
    }
    return -1;
}

// Rotation part of a rigid matrix -> quaternion (the math header has no
// matrix -> quaternion; gm_freeride.cpp / fade_freeride.cpp carry the same
// local helper).
Quaternionf QuaternionFromMatrix( const Matrix4x4f &m ) {
    const float m00 = m.m[0][0], m11 = m.m[1][1], m22 = m.m[2][2];
    const float trace = m00 + m11 + m22;
    if ( trace > 0.0f ) {
        const float s = std::sqrt( trace + 1.0f ) * 2.0f;
        return Quaternionf( ( m.m[1][2] - m.m[2][1] ) / s, ( m.m[2][0] - m.m[0][2] ) / s,
                            ( m.m[0][1] - m.m[1][0] ) / s, 0.25f * s );
    }
    if ( m00 > m11 && m00 > m22 ) {
        const float s = std::sqrt( 1.0f + m00 - m11 - m22 ) * 2.0f;
        return Quaternionf( 0.25f * s, ( m.m[1][0] + m.m[0][1] ) / s, ( m.m[2][0] + m.m[0][2] ) / s,
                            ( m.m[1][2] - m.m[2][1] ) / s );
    }
    if ( m11 > m22 ) {
        const float s = std::sqrt( 1.0f + m11 - m00 - m22 ) * 2.0f;
        return Quaternionf( ( m.m[1][0] + m.m[0][1] ) / s, 0.25f * s, ( m.m[2][1] + m.m[1][2] ) / s,
                            ( m.m[2][0] - m.m[0][2] ) / s );
    }
    const float s = std::sqrt( 1.0f + m22 - m00 - m11 ) * 2.0f;
    return Quaternionf( ( m.m[2][0] + m.m[0][2] ) / s, ( m.m[2][1] + m.m[1][2] ) / s, 0.25f * s,
                        ( m.m[0][1] - m.m[1][0] ) / s );
}

Quaternionf Slerp( const Quaternionf &a, const Quaternionf &b, float t ) {
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    Quaternionf bb = b;
    if ( dot < 0.0f ) {
        dot = -dot;
        bb = Quaternionf( -b.x, -b.y, -b.z, -b.w );
    }
    if ( dot > 0.9995f ) {
        Quaternionf r( a.x + ( bb.x - a.x ) * t, a.y + ( bb.y - a.y ) * t, a.z + ( bb.z - a.z ) * t,
                       a.w + ( bb.w - a.w ) * t );
        const float length = std::sqrt( r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w );
        return length > 0.0f ? Quaternionf( r.x / length, r.y / length, r.z / length, r.w / length ) : a;
    }
    const float theta0 = std::acos( Clamp( dot, -1.0f, 1.0f ) );
    const float theta = theta0 * t;
    const float sinTheta = std::sin( theta );
    const float sinTheta0 = std::sin( theta0 );
    const float s0 = std::cos( theta ) - dot * sinTheta / sinTheta0;
    const float s1 = sinTheta / sinTheta0;
    return Quaternionf( s0 * a.x + s1 * bb.x, s0 * a.y + s1 * bb.y, s0 * a.z + s1 * bb.z, s0 * a.w + s1 * bb.w );
}

// Rigid transform blend: the raised and lowered window poses differ by a
// translation and a small rotation, so decompose into (rotation, translation,
// scale), slerp / lerp and rebuild.
Matrix4x4f BlendRigid( const Matrix4x4f &a, const Matrix4x4f &b, float t ) {
    Vector3f ca[3], cb[3], sa, sb;
    auto decompose = []( const Matrix4x4f &m, Vector3f out[3], Vector3f &scale ) {
        for ( int c = 0; c < 3; c++ ) {
            out[c] = Vector3f( m.m[c][0], m.m[c][1], m.m[c][2] );
        }
        scale = Vector3f( out[0].length(), out[1].length(), out[2].length() );
        const float lengths[3] = { scale.x, scale.y, scale.z };
        for ( int c = 0; c < 3; c++ ) {
            out[c] = lengths[c] > 1e-6f ? out[c] * ( 1.0f / lengths[c] ) : Vector3f( 0.0f, 0.0f, 0.0f );
        }
    };
    decompose( a, ca, sa );
    decompose( b, cb, sb );
    Matrix4x4f ra, rb;
    for ( int c = 0; c < 3; c++ ) {
        ra.m[c][0] = ca[c].x; ra.m[c][1] = ca[c].y; ra.m[c][2] = ca[c].z;
        rb.m[c][0] = cb[c].x; rb.m[c][1] = cb[c].y; rb.m[c][2] = cb[c].z;
    }
    Matrix4x4f result = Matrix4x4f::FromQuaternion( Slerp( QuaternionFromMatrix( ra ), QuaternionFromMatrix( rb ), t ) );
    const Vector3f scale = Lerp( sa, sb, t );
    const float scales[3] = { scale.x, scale.y, scale.z };
    for ( int c = 0; c < 3; c++ ) {
        result.m[c][0] *= scales[c];
        result.m[c][1] *= scales[c];
        result.m[c][2] *= scales[c];
    }
    const Vector3f ta = a.getTranslation(), tb = b.getTranslation();
    result.m[3][0] = ta.x + ( tb.x - ta.x ) * t;
    result.m[3][1] = ta.y + ( tb.y - ta.y ) * t;
    result.m[3][2] = ta.z + ( tb.z - ta.z ) * t;
    return result;
}
} // namespace

CinematicCar::CinematicCar() {}

CinematicCar::~CinematicCar() { unload(); }

// Per-instance texture-cache owners (see the header note): every loaded
// CinematicCar gets its own "<bank path>#<id>" owner so unloading one car
// never releases the textures another car of the same bank is drawing with.
std::string CinematicCar::textureOwnerName( const Bank &bank ) const {
    return bank.getPath() + "#" + std::to_string( ownerId );
}

void CinematicCar::unload() {
    car.destroy();
    interior.destroy();
    rim.destroy();
    rimRear.destroy();
    tire.destroy();
    for ( Bank *pBank : { &carBank, &carCommonBank, &rimBank, &rimRearBank, &interiorBank } ) {
        if ( !pBank->getEntries().empty() ) {
            gpRender->getTextureCache().releaseOwner( textureOwnerName( *pBank ).c_str() );
        }
        pBank->unload();
    }
    wheelNodes.clear();
    wheelShadowNodes.clear();
    wheelSpinRests[0].clear();
    wheelSpinRests[1].clear();
    bWheelSpinRests = false;
    bHardtop = false;
    hardtopTime = 0.0f;
    hardtopSkin.clear();
    bWindowsOpen = false;
    windowTime = windowTarget = 0.0f;
    for ( DashMaterial &entry : dash ) {
        entry = DashMaterial();
    }
    lightMaterials.clear();
    causticNodes.clear();
    flareNodes.clear();
    for ( int32_t i = 0; i < 3; i++ ) {
        lightBoxNodes[i] = -1;
        lightBoxMaterials[i].clear();
    }
    bHeadlights = false;
    bBraking = false;
    bReverse = false;
    indicatorSide = -1;
    indicatorTimer = -1.0f;
    indicatorValue[0] = indicatorValue[1] = 0.0f;
    bIndicatorLamp = false;
    headlightMode = -1;
    highBeamTimer = 0.0f;
    bInTunnel = false;
    bDashboardLit = false;
    cockpitLowNodes.clear();
    interiorDrawnNodes.clear();
    for ( DoorNodes &door : doors ) {
        door = DoorNodes();
    }
    doorAngle[0] = doorAngle[1] = 0.0f;
    for ( WiperHalf &wiper : wipers ) {
        if ( wiper.pIndices != nullptr && gpRender != nullptr ) {
            gpRender->getDevice().destroyBuffer( wiper.pIndices );
        }
    }
    wipers.clear();
    wiperNode = -1;
    wiperAngle = 0.0f;
    wiperTimer = 0.0f;
}

void CinematicCar::assignEnvironment( EdenRenderer &, GPUTexture *pCube, bool bDynamic ) {
    // Car environment cube binding (docs/re/car_glass_spec.md):
    // FUN_00d58df0 (dynamic cube, DAT_011c9f00/04 + 0x48) and FUN_00d58d20
    // (static cube, e.g. the garage CUBECAR) both end in FUN_00d58a90: for
    // the materials of the car families below, FUN_00d56df0 replaces the
    // texture of every slot whose texture hashcode is the default car cube
    // (DAT_011f8e88: "CUBEMAP" of common_car.bnk [INF: the global is set in
    // encrypted code; FUN_00c4b0e0 looks the default cube up by it]); other
    // cubes (CUBEINT of the interior) are kept. When the texture changed
    // and S_USEREFLECTION (spec 3) is set, FUN_00d58980 writes 1 (static
    // cube) or 0 (dynamic cube) to SPECPARM.z (TD_CarPaint family) or
    // RAMPPOW.z (TD_CarPaint2 family): the car shaders scale the
    // reflection by (1 - cube.a * z).
    static const uint64_t kDefaultCube = GetIdentifier64bit( "CUBEMAP" );
    static const uint64_t kSpecParm = GetIdentifier64bit( "SPECPARM" );
    static const uint64_t kRampPow = GetIdentifier64bit( "RAMPPOW" );
    if ( pCube == nullptr ) {
        return;
    }
    for ( EdenModel *pModel : { &car, &interior, &rim, &rimRear, &tire } ) {
        for ( EdenMaterial &material : pModel->getMaterials() ) {
            const uint64_t family = material.ShaderSetGUID;
            // FUN_00d58a90 family list. TD_Mirror (0x08041973fadeffff) is in
            // the original only with the car env +0x2ab6 flag [ENC]; enabled
            // unconditionally here: the wing mirror otherwise keeps the
            // default outdoor CUBEMAP inside closed interiors (the garage
            // mirror showed palms, session 45) [INF: the flag gates nothing
            // visible in the reachable states].
            const bool bCarFamily = family == 0x183157b81c6c7357ull || family == 0xca00000000000010ull ||
                                    family == 0x183157b81c6c5377ull || family == 0xdddddddd1c6c7357ull ||
                                    family == 0xb39c35015789e65cull || family == 0xca00000000000020ull ||
                                    family == 0x6638348331d66466ull || family == 0x1b38348331cacacaull ||
                                    family == 0x08041973fadeffffull;
            if ( !bCarFamily ) {
                continue;
            }
            bool bChanged = false;
            for ( uint32_t i = 0; i < material.NumSlots; i++ ) {
                if ( material.TextureHashes[i] == kDefaultCube && material.pTextures[i] != pCube ) {
                    material.pTextures[i] = pCube;
                    bChanged = true;
                }
            }
            if ( !bChanged || material.Spec[3] == 0 ) {
                continue;
            }
            uint64_t parameter = 0;
            if ( family == 0x183157b81c6c7357ull || family == 0xca00000000000010ull || family == 0x1b38348331cacacaull ) {
                parameter = kSpecParm;
            } else if ( family == 0x183157b81c6c5377ull || family == 0x183157b81c6ccacaull ) {
                parameter = kRampPow;
            }
            auto it = parameter != 0 ? material.Parameters.find( parameter ) : material.Parameters.end();
            if ( it != material.Parameters.end() ) {
                it->second.z = bDynamic ? 0.0f : 1.0f;
            }
        }
    }
}

bool CinematicCar::load( const Desc &requested ) {
    unload();
    gpsMaterials.clear();
    gpsNodes.clear();
    bGpsBound = false;
    dashboardSpeedNode = dashboardRpmNode = -1;
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    const std::string folder = gpFile->getBankFolder() + "vehicules/";
    Desc desc = requested;
    if ( !gpFile->fileExists( ( folder + "high/" + ToLower( desc.FileName ) + ".bnk" ).c_str() ) &&
         ( desc.ModelBank.empty() ||
           !gpFile->fileExists( ( folder + "high/" + ToLower( desc.ModelBank ) + ".bnk" ).c_str() ) ) ) {
        OTDU_LOG_WARN( "CinematicCar: '%s' not available, using the California\n", desc.FileName.c_str() );
        desc = Desc();
    }
    fileName = desc.FileName;
    // The new private owners (unload above released the previous ones).
    static uint32_t nextOwnerId = 0;
    ownerId = ++nextOwnerId;
    // The bank the geometry comes from (Desc::ModelBank; FileName unless set).
    const std::string carName = ToLower( desc.ModelBank.empty() ? desc.FileName : desc.ModelBank );
    // A lower level of detail (Desc::Lod): vehicules/med/<car>_med.bnk or
    // <car>_low.bnk, the model named <car>_<lod> [DATA: the banks of the
    // shipped data, e.g. tts_med.bnk holds tts_med.3dd/3dg, med_tts_*.2db,
    // tts_color.2dm]. The high bank when the car ships no such bank.
    std::string modelName = carName;
    std::string carPath = folder + "high/" + carName + ".bnk";
    if ( !desc.Lod.empty() ) {
        const std::string lodPath = folder + "med/" + carName + "_" + desc.Lod + ".bnk";
        if ( gpFile->fileExists( lodPath.c_str() ) ) {
            modelName = carName + "_" + desc.Lod;
            carPath = lodPath;
        } else {
            OTDU_LOG_INFO( "CinematicCar: %s has no %s bank, using the high one\n", carName.c_str(), desc.Lod.c_str() );
        }
    }
    lodName = modelName == carName ? std::string() : desc.Lod;
    const std::string commonPath = folder + "common_car.bnk";
    if ( !carCommonBank.load( commonPath.c_str() ) || !carBank.load( carPath.c_str() ) ) {
        return false;
    }
    // Named after the load: before it the bank paths are empty, both owners
    // were "#<id>" (one merged owner) and unload() released "<path>#<id>",
    // which never matched - the car textures leaked on every load.
    const std::string commonOwner = textureOwnerName( carCommonBank );
    const std::string carOwner = textureOwnerName( carBank );
    TextureCache &textures = gpRender->getTextureCache();
    textures.loadBankTextures( carCommonBank, commonOwner.c_str() );
    textures.loadBankTextures( carBank, carOwner.c_str() );
    // Interior bank (vehicules/interiors/<car>_i.bnk): the cockpit maps
    // (california_int*.2db...) used by the interior materials.
    const std::string interiorPath = folder + "interiors/" + carName + "_i.bnk";
    if ( desc.bInterior && gpFile->fileExists( interiorPath.c_str() ) && interiorBank.load( interiorPath.c_str() ) ) {
        const std::string interiorOwner = textureOwnerName( interiorBank );
        textures.loadBankTextures( interiorBank, interiorOwner.c_str() );
        // The cockpit model (<car>_i.3dd, car frame) [INF: when the game
        // draws it is not read; drawn with the car].
        interior.load( *pRenderer, interiorBank, ( carName + "_i" ).c_str(), nullptr, interiorOwner.c_str() );
        // The cockpit gauge needles: the needle mesh is ND_SPD / ND_RPM when
        // the model has a separate mesh (California), otherwise the _1 pose
        // node itself carries the geometry (R8). The two ND_x_N nodes are the
        // two rest poses of the retail code; the port drives the mesh from its
        // own rest pose (world transforms, see setDashboardNeedles).
        const auto nodeHasGeometry = [&]( int32_t node ) {
            for ( const EdenModel::Item &item : interior.getItems() ) {
                if ( item.NodeIndex == node ) {
                    return true;
                }
            }
            return false;
        };
        const auto findNeedle = [&]( const char *pPoseName, const char *pMeshName, int32_t &outNode,
                                     Matrix4x4f &outRest ) {
            const int32_t pose = interior.getScene().findNode( pPoseName );
            int32_t mesh = interior.getScene().findNode( pMeshName );
            if ( mesh < 0 || !nodeHasGeometry( mesh ) ) {
                mesh = pose;
            }
            if ( mesh >= 0 && nodeHasGeometry( mesh ) ) {
                outNode = mesh;
                outRest = interior.getNodeTransform( mesh );
            }
        };
        findNeedle( "ND_SPD_1", "ND_SPD", dashboardSpeedNode, dashboardSpeedRest );
        findNeedle( "ND_RPM_1", "ND_RPM", dashboardRpmNode, dashboardRpmRest );
        OTDU_LOG_INFO( "CinematicCar: cockpit needles speed node %d, rpm node %d\n", dashboardSpeedNode,
                       dashboardRpmNode );
        // -needletest <node>:<x|y|z>:<deg>: rotate one cockpit node about its
        // local axis, to identify the gauge needle pivot / sweep on a new car
        // model (docs/re/needles_spec.md).
        if ( const char *pNeedleTest = CmdLine::GetArgValue( "-needletest" ) ) {
            char nodeName[64] = {};
            char axis = 'z';
            float degrees = 0.0f;
            if ( sscanf( pNeedleTest, "%63[^:]:%c:%f", nodeName, &axis, &degrees ) >= 3 ) {
                // (the mesh node is the one to move: this model keeps world
                // transforms, so moving a pose parent does not move its child)
                const int32_t node = interior.getScene().findNode( nodeName );
                if ( node >= 0 ) {
                    const Vector3f axisVector = axis == 'x' ? Vector3f( 1.0f, 0.0f, 0.0f )
                                               : axis == 'y' ? Vector3f( 0.0f, 1.0f, 0.0f )
                                                             : Vector3f( 0.0f, 0.0f, 1.0f );
                    const Quaternionf rotation =
                        Quaternionf::FromAxisAngle( axisVector, degrees * kPI / 180.0f );
                    interior.setNodeTransform( node, interior.getNodeTransform( node ) *
                                                         Matrix4x4f::FromQuaternion( rotation ) );
                    OTDU_LOG_INFO( "CinematicCar: -needletest '%s' around %c by %.1f deg\n", nodeName, axis, degrees );
                } else {
                    OTDU_LOG_WARN( "CinematicCar: -needletest '%s' not in the interior model\n", nodeName );
                }
            }
        }
        // -needlepose <pose>:<mesh>: put a needle mesh at one of its pose
        // nodes (ND_x_1 = value 0, ND_x_2 = max), to calibrate the sweep.
        if ( const char *pNeedlePose = CmdLine::GetArgValue( "-needlepose" ) ) {
            char poseName[64] = {};
            char meshName[64] = {};
            if ( sscanf( pNeedlePose, "%63[^:]:%63s", poseName, meshName ) == 2 ) {
                const int32_t pose = interior.getScene().findNode( poseName );
                const int32_t mesh = interior.getScene().findNode( meshName );
                if ( pose >= 0 && mesh >= 0 ) {
                    interior.setNodeTransform( mesh, interior.getNodeTransform( pose ) );
                    OTDU_LOG_INFO( "CinematicCar: -needlepose '%s' -> '%s'\n", meshName, poseName );
                }
            }
        }
    }
    // The wiper split (collectWipers) needs each item's primitive data.
    car.setKeepItemIndices( true );
    if ( !car.load( *pRenderer, carBank, modelName.c_str(), nullptr, carOwner.c_str() ) ) {
        return false;
    }
    if ( rimBank.load( ( folder + "rim/" + desc.RimFront + ".bnk" ).c_str() ) ) {
        const std::string rimOwner = textureOwnerName( rimBank );
        textures.loadBankTextures( rimBank, rimOwner.c_str() );
        rim.load( *pRenderer, rimBank, nullptr, nullptr, rimOwner.c_str() );
        // The rims of several cars (One-77...) are wound the other way round
        // than their culling state: closed meshes, drawn without culling.
        for ( EdenMaterial &material : rim.getMaterials() ) {
            material.CullMode = 1;
        }
    }
    if ( desc.RimRear != desc.RimFront && rimRearBank.load( ( folder + "rim/" + desc.RimRear + ".bnk" ).c_str() ) ) {
        const std::string rimRearOwner = textureOwnerName( rimRearBank );
        textures.loadBankTextures( rimRearBank, rimRearOwner.c_str() );
        rimRear.load( *pRenderer, rimRearBank, nullptr, nullptr, rimRearOwner.c_str() );
        for ( EdenMaterial &material : rimRear.getMaterials() ) {
            material.CullMode = 1;
        }
    }
    // The tire model: one mesh per rim diameter (T_13P .. T_24P). The
    // GeometrySetup remaps every base mesh to the car's stock tire radius
    // (plus sizing, see RemapTireProfiles), so a tuning shop rim size is a
    // node / scale change at run time.
    const float stockTireRadius = desc.TireRadius;
    tire.load( *pRenderer, carCommonBank, "tire", nullptr, commonOwner.c_str(), nullptr, nullptr,
               [stockTireRadius]( Render3DG &geometry ) { RemapTireProfiles( geometry, stockTireRadius ); } );

    // Exterior, high resolution (FUN_00c66380, LOD 3 / 4: D_CAR shown,
    // BODY_MR / BODY_LR and the static roofs HARDTOP_MR / HARDTOP_LR hidden;
    // the retractable roof is the skinned HANIM_S of D_CAR), *_LR paints,
    // the light volumes / caustics (night only) and the projected shadow
    // boxes (they need the scene depth) hidden.
    const Render3DD &scene = car.getScene();
    const std::vector<SceneNode> &nodes = scene.getNodes();
    std::vector<int32_t> hiddenRoots;
    const uint64_t hardtopMR = NodeNameHash( "HARDTOP_MR" ), hardtopLR = NodeNameHash( "HARDTOP_LR" );
    const uint64_t hanimMR = NodeNameHash( "HANIM_MR" );
    for ( size_t i = 0; i < nodes.size(); i++ ) {
        const std::string &name = nodes[i].Name;
        if ( name == "BODY_MR" || name == "BODY_LR" || nodes[i].Hashcode == hardtopMR ||
             nodes[i].Hashcode == hardtopLR || nodes[i].Hashcode == hanimMR ||
             name.find( "BREAKI" ) != std::string::npos || name.find( "EADLI" ) != std::string::npos ||
             name.find( "EVERS" ) != std::string::npos || name.rfind( "?A_", 0 ) == 0 ) {
            hiddenRoots.push_back( static_cast<int32_t>( i ) );
        }
    }
    for ( size_t i = 0; i < nodes.size(); i++ ) {
        for ( int32_t root : hiddenRoots ) {
            if ( static_cast<int32_t>( i ) == root || scene.isDescendantOf( static_cast<int32_t>( i ), root ) ) {
                car.setNodeVisible( static_cast<int32_t>( i ), false );
            }
        }
    }
    // The *_LR paints: a node whose items are all low resolution paint hides
    // (the LOD duplicates of the paint), a node that also holds high resolution
    // items hides only those items. Hiding the node of any Paint_LR item hid
    // the whole body shell of the Q7 V12 - its BODY_HR carries one 136-vertex
    // Paint_LR prim by the headlamps (-logcaritems BODY_HR): bonnet, wings and
    // pillars gone, the doors and bumpers left (the owner, 2026-11-05) [DATA].
    lowResItems.clear();
    {
        const std::vector<EdenModel::Item> &carItems = car.getItems();
        std::vector<int32_t> lowCount( nodes.size(), 0 ), itemCount( nodes.size(), 0 );
        const auto isLowRes = [&]( const EdenModel::Item &item ) {
            const std::string &materialName = car.getMaterials()[item.MaterialIndex].Name;
            return materialName == "Paint_LR" || materialName == "Plate_LR";
        };
        for ( const EdenModel::Item &item : carItems ) {
            if ( item.NodeIndex >= 0 && item.NodeIndex < static_cast<int32_t>( nodes.size() ) ) {
                itemCount[item.NodeIndex]++;
                lowCount[item.NodeIndex] += isLowRes( item ) ? 1 : 0;
            }
        }
        for ( size_t i = 0; i < carItems.size(); i++ ) {
            const EdenModel::Item &item = carItems[i];
            if ( item.NodeIndex < 0 || item.NodeIndex >= static_cast<int32_t>( nodes.size() ) || !isLowRes( item ) ) {
                continue;
            }
            static const bool bNodeHide = CmdLine::HasArg( "-lrnodehide" ); // A/B: the old rule
            if ( lowCount[item.NodeIndex] == itemCount[item.NodeIndex] || bNodeHide ) {
                car.setNodeVisible( item.NodeIndex, false );
            } else if ( car.isNodeVisible( item.NodeIndex ) ) { // BODY_MR / _LR are hidden whole already
                lowResItems.push_back( static_cast<int32_t>( i ) );
                OTDU_LOG_INFO( "CinematicCar: '%s' item %zu ('%s') hidden in node '%s' (%d of %d items low res)\n",
                               fileName.c_str(), i, car.getMaterials()[item.MaterialIndex].Name.c_str(),
                               nodes[item.NodeIndex].Name.c_str(), lowCount[item.NodeIndex], itemCount[item.NodeIndex] );
            }
        }
        if ( !lowResItems.empty() ) {
            OTDU_LOG_INFO( "CinematicCar: '%s' %zu low resolution paint item(s) hidden by item (their node has high "
                           "resolution parts)\n",
                           fileName.c_str(), lowResItems.size() );
        }
    }
    // Cockpit (FUN_00c66380): COCK_LR and the door panels INT_LR_L / R
    // (children of DOOR_L / R) at LOD 2 / 3; at LOD 4 the interior bank
    // instances (FUN_00c69e30): COCK_HR (its seat belts BELT_L / R only
    // when flagged, +0xadd8 / +0xadd9, 0 at creation) and INT_HR_L / R.
    // The interior root GPS and GLASS_I (material carrier of the inside
    // glass, FUN_00c53d10) are not instanced.
    cockpitLowNodes.clear();
    for ( const char *pName : { "COCK_LR", "INT_LR_L", "INT_LR_R" } ) {
        const int32_t root = scene.findNode( pName );
        for ( size_t i = 0; root >= 0 && i < nodes.size(); i++ ) {
            if ( static_cast<int32_t>( i ) == root || scene.isDescendantOf( static_cast<int32_t>( i ), root ) ) {
                cockpitLowNodes.push_back( static_cast<int32_t>( i ) );
            }
        }
    }
    // The glass the driver looks through (draw(): hidden in the inside views).
    glassNodes.clear();
    for ( const char *pName : { "GLASS_F", "WND_L1", "WND_R1" } ) {
        const int32_t node = scene.findNode( pName );
        if ( node >= 0 ) {
            glassNodes.push_back( node );
        }
    }
    OTDU_LOG_INFO( "CinematicCar: glass nodes %zu (GLASS_F / WND_L1 / R1)\n", glassNodes.size() );
    interiorDrawnNodes.clear();
    gpsNodes.clear();
    if ( interior.isLoaded() ) {
        const Render3DD &interiorScene = interior.getScene();
        const std::vector<SceneNode> &interiorNodes = interiorScene.getNodes();
        std::vector<int32_t> roots, excluded, gpsRoots;
        for ( const char *pName : { "COCK_HR", "INT_HR_L", "INT_HR_R" } ) {
            const int32_t node = interiorScene.findNode( pName );
            if ( node >= 0 ) {
                roots.push_back( node );
            }
        }
        // BELT_L / R (shown only when flagged, +0xadd8/+0xadd9, 0 at
        // creation) and GPS: the dashboard GPS screen quad needs the runtime
        // projection params TDU1 writes in setGPSTexture (vehicle_base.cpp:729,
        // static params [0]=minimap U, [1]=V, [2]=3.0 into the TD_GPS
        // material 7dad16b47eb57b95); unprojected it floats across the scene
        // as a black sail (session 45). Hidden until the dash GPS texture is
        // bound (setGpsTexture re-shows the subtrees).
        for ( const char *pName : { "BELT_L", "BELT_R" } ) {
            const int32_t node = interiorScene.findNode( pName );
            if ( node >= 0 ) {
                excluded.push_back( node );
            }
        }
        for ( size_t i = 0; i < interiorNodes.size(); i++ ) {
            if ( interiorNodes[i].Name == "GPS" ) {
                excluded.push_back( static_cast<int32_t>( i ) );
                gpsRoots.push_back( static_cast<int32_t>( i ) );
            }
        }
        for ( size_t i = 0; i < interiorNodes.size(); i++ ) {
            const int32_t node = static_cast<int32_t>( i );
            auto inSubtree = [&]( const std::vector<int32_t> &list ) {
                for ( int32_t root : list ) {
                    if ( node == root || interiorScene.isDescendantOf( node, root ) ) {
                        return true;
                    }
                }
                return false;
            };
            const bool bDrawn = inSubtree( roots ) && !inSubtree( excluded );
            interior.setNodeVisible( node, bDrawn );
            if ( bDrawn ) {
                interiorDrawnNodes.push_back( node );
            }
        }
        // The GPS subtrees (there can be more than one: the root screen and
        // the cockpit copy) for the re-show.
        for ( int32_t root : gpsRoots ) {
            for ( size_t i = 0; i < interiorNodes.size(); i++ ) {
                const int32_t node = static_cast<int32_t>( i );
                if ( node == root || interiorScene.isDescendantOf( node, root ) ) {
                    gpsNodes.push_back( node );
                }
            }
        }
    }

    // Retractable hardtop: HANIM_S skinned on <car>.xsb, posed by the
    // car's 'Hardtop' animation player (FUN_00c67560: \HardTop.anm; player
    // at +0xb8d0, time +0x8c4 = 0 and mode +0x8d0 = 2 at creation,
    // FUN_00c68c80). Time 0 is the closed roof (FUN_00c662b0 == 1 shows the
    // static HARDTOP_MR / LR, the car wash closes it with FUN_00c668c0);
    // FUN_00c3fcc0 opens it (FUN_00c66920(1.0): time = 0.99999 x duration)
    // and lowers the side windows (FUN_00c66150 / FUN_00c66110(1.0): WND_x1
    // moved onto WND_x2, FUN_00c674a0) for the cars whose _car_body is 2 or
    // 4 (roadsters / convertibles) when they are created (FUN_00a51aa0) or
    // given back at the first freeride (FUN_00b10da0 step 3).
    bHardtop = false;
    hardtopSkin.clear();
    // Side window rests, captured before the retractable roof lowers WND_x1
    // onto WND_x2 (the run time window switch needs both poses).
    for ( int32_t side = 0; side < 2; side++ ) {
        const char *pSide = side == 0 ? "L" : "R";
        windows[side] = WindowNodes();
        windows[side].Raised = scene.findNode( ( std::string( "WND_" ) + pSide + "1" ).c_str() );
        const int32_t lowered = scene.findNode( ( std::string( "WND_" ) + pSide + "2" ).c_str() );
        if ( windows[side].Raised >= 0 ) {
            windows[side].RaisedRest = car.getNodeTransform( windows[side].Raised );
        }
        if ( lowered >= 0 ) {
            windows[side].LoweredRest = car.getNodeTransform( lowered );
        }
    }
    const BankEntry *pSkeleton = carBank.findEntry( ( carName + ".xsb" ).c_str() );
    if ( pSkeleton == nullptr ) {
        const std::vector<const BankEntry *> skeletons = carBank.findEntriesByExtension( "xsb" );
        pSkeleton = skeletons.empty() ? nullptr : skeletons.front();
    }
    const BankEntry *pHardtop = carBank.findEntry( "Hardtop.anm" );
    if ( pSkeleton != nullptr && pHardtop != nullptr && hardtopSkeleton.parse( pSkeleton->pData, pSkeleton->Size ) &&
         hardtopAnimation.parse( pHardtop->pData, pHardtop->Size ) ) {
        const int32_t carBody = FindCarBody( desc.FileName );
        const bool bOpen = carBody == 2 || carBody == 4;
        hardtopTime = bOpen ? 0.99999f * hardtopAnimation.getDuration() : 0.0f;
        hardtopTarget = hardtopTime;
        bWindowsOpen = hardtopTime > 0.0f;
        static const int32_t forced = CmdLine::GetArgInt( "-hardtop", -1 );
        if ( forced == 0 || forced == 1 ) {
            hardtopTime = forced == 1 ? 0.99999f * hardtopAnimation.getDuration() : 0.0f;
        }
        std::vector<BoneTransform> locals( hardtopSkeleton.getNumBones() );
        for ( uint32_t bone = 0; bone < hardtopSkeleton.getNumBones(); bone++ ) {
            locals[bone] = hardtopSkeleton.getBindLocal( bone );
        }
        hardtopAnimation.sample( hardtopSkeleton, hardtopTime, false, locals );
        std::vector<Matrix4x4f> boneWorld;
        hardtopSkeleton.computeSkinMatrices( locals, boneWorld, hardtopSkin );
        bHardtop = true;
        if ( hardtopTime > 0.0f ) {
            for ( const char *pSide : { "L", "R" } ) {
                const int32_t window = scene.findNode( ( std::string( "WND_" ) + pSide + "1" ).c_str() );
                const int32_t lowered = scene.findNode( ( std::string( "WND_" ) + pSide + "2" ).c_str() );
                if ( window >= 0 && lowered >= 0 ) {
                    car.setNodeTransform( window, car.getNodeTransform( lowered ) );
                }
            }
        }
        OTDU_LOG_INFO( "CinematicCar: '%s' _car_body %d, hardtop %s (Hardtop.anm t = %.2f / %.2f)\n",
                       desc.FileName.c_str(), carBody, hardtopTime > 0.0f ? "open" : "closed", hardtopTime,
                       hardtopAnimation.getDuration() );
        if ( CmdLine::HasArg( "-logskin" ) ) {
            // A/B diagnostic (meeting_tess black shards): every skinned item of
            // the car with its palette extent against the Hardtop skeleton.
            OTDU_LOG_INFO( "CinematicCar: hardtopSkin %zu matrices, skeleton %u bones\n", hardtopSkin.size(),
                           hardtopSkeleton.getNumBones() );
            const std::vector<SceneNode> &carNodes = car.getScene().getNodes();
            std::vector<uint32_t> skinnedCount( carNodes.size(), 0 ), maxBone( carNodes.size(), 0 );
            std::vector<bool> hasItems( carNodes.size(), false );
            for ( const auto &item : car.getItems() ) {
                if ( item.NodeIndex >= carNodes.size() ) {
                    continue;
                }
                hasItems[item.NodeIndex] = true;
                if ( item.BonePalette.empty() ) {
                    continue;
                }
                skinnedCount[item.NodeIndex]++;
                for ( uint32_t bone : item.BonePalette ) {
                    maxBone[item.NodeIndex] = Max( maxBone[item.NodeIndex], bone );
                }
            }
            for ( size_t i = 0; i < carNodes.size(); i++ ) {
                if ( skinnedCount[i] != 0 ) {
                    OTDU_LOG_INFO( "CinematicCar: skinned node %zu '%s': %u items, max bone %u\n", i,
                                   carNodes[i].Name.c_str(), skinnedCount[i], maxBone[i] );
                }
            }
            // Nodes carrying the dashboard GPS screen material (TDU1
            // setGPSTexture: shader guid 7dad16b47eb57b95; the projection
            // params are written at run time, the raw mesh is the "black
            // sail" of the meeting_tess in-car shot when left unprojected).
            for ( EdenModel *pModel : { &car, &interior } ) {
                if ( !pModel->isLoaded() ) {
                    continue;
                }
                const std::vector<SceneNode> &modelNodes = pModel->getScene().getNodes();
                for ( const auto &item : pModel->getItems() ) {
                    if ( item.NodeIndex >= modelNodes.size() ) {
                        continue;
                    }
                    const EdenMaterial &material = pModel->getMaterials()[item.MaterialIndex];
                    if ( material.Name == "Leather" ) {
                        OTDU_LOG_INFO( "CinematicCar: Leather on %s node %u '%s' palette %zu\n",
                                       pModel == &interior ? "interior" : "car", item.NodeIndex,
                                       modelNodes[item.NodeIndex].Name.c_str(), item.BonePalette.size() );
                    }
                    if ( material.ShaderSetGUID == kGpsShaderGuid ) {
                        OTDU_LOG_INFO( "CinematicCar: GPS screen mesh on %s node %u '%s' (material '%s')\n",
                                       pModel == &interior ? "interior" : "car", item.NodeIndex,
                                       modelNodes[item.NodeIndex].Name.c_str(), material.Name.c_str() );
                    }
                }
            }
            for ( size_t i = 0; i < carNodes.size(); i++ ) {
                OTDU_LOG_INFO( "CinematicCar: node %zu '%s' parent %d%s\n", i, carNodes[i].Name.c_str(),
                               carNodes[i].Parent, hasItems[i] ? " [items]" : "" );
            }
            if ( interior.isLoaded() ) {
                const Render3DD &interiorScene = interior.getScene();
                const std::vector<SceneNode> &iNodes = interiorScene.getNodes();
                for ( size_t i = 0; i < iNodes.size(); i++ ) {
                    const Matrix4x4f &m = iNodes[i].World;
                    OTDU_LOG_INFO( "CinematicCar: interior node %zu '%s' parent %d t (%.2f %.2f %.2f)\n", i,
                                   iNodes[i].Name.c_str(), iNodes[i].Parent, m.m[3][0], m.m[3][1], m.m[3][2] );
                }
            }
        }
    } else {
        // No skeleton / Hardtop.anm: no pose to skin HANIM_S with, hidden.
        const int32_t hanim = scene.findNode( "HANIM_S" );
        for ( size_t i = 0; hanim >= 0 && i < nodes.size(); i++ ) {
            if ( static_cast<int32_t>( i ) == hanim || scene.isDescendantOf( static_cast<int32_t>( i ), hanim ) ) {
                car.setNodeVisible( static_cast<int32_t>( i ), false );
            }
        }
    }

    // The window animation starts where the load left the glass (the
    // convertibles open with the roof, so the windows start lowered).
    windowTime = windowTarget = bWindowsOpen ? 1.0f : 0.0f;

    // Doors (FUN_00c6a610): DOOR_L / DOOR_R (slots 29 / 30); their key
    // nodes DOOR_x_A_1..4 (strings 0x00ef5d38 / 0x00ef5d2c, up to 3 channels
    // A..C) do not exist in california.3dd [DATA], so the door is rotated
    // about its own Y axis (FUN_00c67020 fallback). Rest transforms are taken
    // after the window placement above.
    for ( int32_t side = 0; side < 2; side++ ) {
        DoorNodes &door = doors[side];
        door = DoorNodes();
        door.Door = scene.findNode( side == 0 ? "DOOR_L" : "DOOR_R" );
        if ( door.Door < 0 ) {
            continue;
        }
        door.DoorRest = car.getNodeTransform( door.Door );
        for ( size_t i = 0; i < nodes.size(); i++ ) {
            const int32_t node = static_cast<int32_t>( i );
            if ( node == door.Door || scene.isDescendantOf( node, door.Door ) ) {
                door.CarRest.emplace_back( node, car.getNodeTransform( node ) );
            }
        }
        if ( interior.isLoaded() ) {
            const Render3DD &interiorScene = interior.getScene();
            const int32_t panel = interiorScene.findNode( side == 0 ? "INT_HR_L" : "INT_HR_R" );
            for ( size_t i = 0; panel >= 0 && i < interiorScene.getNodes().size(); i++ ) {
                const int32_t node = static_cast<int32_t>( i );
                if ( node == panel || interiorScene.isDescendantOf( node, panel ) ) {
                    door.InteriorRest.emplace_back( node, interior.getNodeTransform( node ) );
                }
            }
        }
    }
    doorAngle[0] = doorAngle[1] = 0.0f;

    // A/B: -hidenode <name[;name...]> hides node subtrees (identify a stray
    // surface by elimination). The car scene first, then the interior scene.
    // Hidden nodes stay hidden: the per-frame cockpit LOD re-show must skip
    // them, so they are remembered in alwaysHiddenNodes.
    if ( const char *pHideList = CmdLine::GetArgValue( "-hidenode" ) ) {
        std::string list = pHideList;
        size_t start = 0;
        while ( start < list.size() ) {
            const size_t end = list.find( ';', start );
            const std::string name = list.substr( start, end == std::string::npos ? end : end - start );
            start = end == std::string::npos ? list.size() : end + 1;
            if ( name.empty() ) {
                continue;
            }
            bool bFound = false;
            for ( EdenModel *pModel : { &car, &interior } ) {
                if ( !pModel->isLoaded() ) {
                    continue;
                }
                const Render3DD &modelScene = pModel->getScene();
                const int32_t node =
                    name[0] == '#' ? atoi( name.c_str() + 1 ) : modelScene.findNode( name.c_str() );
                if ( node < 0 ) {
                    continue;
                }
                const std::vector<SceneNode> &modelNodes = modelScene.getNodes();
                for ( size_t i = 0; i < modelNodes.size(); i++ ) {
                    if ( static_cast<int32_t>( i ) == node ||
                         modelScene.isDescendantOf( static_cast<int32_t>( i ), node ) ) {
                        pModel->setNodeVisible( static_cast<int32_t>( i ), false );
                        alwaysHiddenNodes[pModel == &interior].push_back( static_cast<int32_t>( i ) );
                    }
                }
                OTDU_LOG_INFO( "CinematicCar: %s node '%s' (%d) hidden\n",
                               pModel == &interior ? "interior" : "car", name.c_str(), node );
                bFound = true;
                break;
            }
            if ( !bFound ) {
                OTDU_LOG_WARN( "CinematicCar: -hidenode '%s' not found\n", name.c_str() );
            }
        }
    }

    // Wheels: rim at D_RIM_xx, tire at the next node (?_TIRE_xx). Tire mesh
    // of the rim diameter (19" for the California: T_19P).
    wheelNodes.clear();
    for ( size_t i = 0; i + 1 < nodes.size(); i++ ) {
        if ( nodes[i].Name.rfind( "D_RIM_", 0 ) == 0 ) {
            wheelNodes.push_back( static_cast<int32_t>( i ) );
        }
    }
    for ( int32_t node : wheelNodes ) {
        const Matrix4x4f &m = nodes[node + 1].World;
        OTDU_LOG_DEBUG( "wheel %s: %.2f %.2f %.2f | %.2f %.2f %.2f | %.2f %.2f %.2f\n", nodes[node].Name.c_str(),
                        m.m[0][0], m.m[0][1], m.m[0][2], m.m[1][0], m.m[1][1], m.m[1][2], m.m[2][0], m.m[2][1],
                        m.m[2][2] );
    }
    // -logcar: the cockpit gauge needle nodes (D_ND_SPD / D_ND_RPM and their
    // ND_x_1 / ND_x_2 poses). The interior needles are model nodes, rotated
    // between the two poses by FUN_00c52240; this dump is the reference for
    // driving them (docs/re/freeride_spec.md 4b).
    if ( CmdLine::HasArg( "-logcar" ) ) {
        const auto dumpNeedleNodes = []( const char *pModel, EdenModel &model ) {
            if ( !model.isLoaded() ) {
                return;
            }
            const std::vector<SceneNode> &modelNodes = model.getScene().getNodes();
            for ( size_t i = 0; i < modelNodes.size(); i++ ) {
                const std::string &name = modelNodes[i].Name;
                if ( name.find( "ND_" ) == std::string::npos && name.find( "AIG" ) == std::string::npos &&
                     name.find( "SPD" ) == std::string::npos && name.find( "RPM" ) == std::string::npos ) {
                    continue;
                }
                const Matrix4x4f &m = model.getNodeTransform( static_cast<int32_t>( i ) );
                const Vector3f t = m.getTranslation();
                Vector3f boxMin( 1e9f, 1e9f, 1e9f ), boxMax( -1e9f, -1e9f, -1e9f );
                for ( const EdenModel::Item &item : model.getItems() ) {
                    if ( item.NodeIndex != static_cast<int32_t>( i ) ) {
                        continue;
                    }
                    boxMin = Vector3f( Min( boxMin.x, item.BoundsMin.x ), Min( boxMin.y, item.BoundsMin.y ),
                                       Min( boxMin.z, item.BoundsMin.z ) );
                    boxMax = Vector3f( Max( boxMax.x, item.BoundsMax.x ), Max( boxMax.y, item.BoundsMax.y ),
                                       Max( boxMax.z, item.BoundsMax.z ) );
                }
                OTDU_LOG_INFO( "CinematicCar %s node %zu '%s' parent %d pos (%.3f %.3f %.3f) rot (%.3f %.3f %.3f | "
                               "%.3f %.3f %.3f | %.3f %.3f %.3f) box (%.3f %.3f %.3f)-(%.3f %.3f %.3f)\n",
                               pModel, i, name.c_str(), modelNodes[i].Parent, t.x, t.y, t.z, m.m[0][0], m.m[0][1],
                               m.m[0][2], m.m[1][0], m.m[1][1], m.m[1][2], m.m[2][0], m.m[2][1], m.m[2][2],
                               boxMin.x, boxMin.y, boxMin.z, boxMax.x, boxMax.y, boxMax.z );
            }
        };
        dumpNeedleNodes( "car", car );
        dumpNeedleNodes( "interior", interior );
    }
    // Wheel contact shadow boxes (children of D_SBOX, unit boxes placed at
    // run time under each tire).
    wheelShadowNodes.clear();
    for ( size_t i = 0; i < nodes.size(); i++ ) {
        if ( nodes[i].Name.find( "WHEEL_" ) != std::string::npos && nodes[i].Parent >= 0 &&
             nodes[nodes[i].Parent].Name == "D_SBOX" ) {
            wheelShadowNodes.push_back( static_cast<int32_t>( i ) );
        }
    }
    // The rim diameters drawn: the Desc's (a tuning shop plus size overrides
    // them). The tire nodes follow (T_<diameter>P).
    rimDiameterFront = desc.RimDiameterFront > 0 ? desc.RimDiameterFront : desc.DiameterFront;
    rimDiameterRear = desc.RimDiameterRear > 0 ? desc.RimDiameterRear : desc.DiameterRear;
    tireRadius = desc.TireRadius;
    // The model's wheel hubs must coincide with the physics wheel: the ray
    // suspension expects the hub at the tire radius above the contact, and a
    // car whose D_RIM_ nodes are authored lower (the R8 V10: 0.168 m under a
    // 0.324 m tire) started with the springs at a permanent deep compression
    // and could not move at all (-vehcheck: top 0.3 km/h). Lift the wheel node
    // and its tire child (the next node) to the tire radius; the drawn tire
    // then rests exactly where the physics ray does [INF].
    //
    // The brake assembly is authored at that same low hub and is *not* under
    // the rim node (FORK_xx > upright > DISC_xx > BRAKE_xx / D_RIM_xx, with
    // STIR_xx the caliper beside DISC_xx), so lifting only the rim left the
    // disc and the caliper `lift` metres under the wheel centre: on the
    // California (0.120 m) the caliper sat by the bottom of the rim instead of
    // beside the disc, and the glow quad - drawn at the physics hub - did not
    // line up with the model's own disc. They take the same lift.
    // The wheels are NOT moved any more (the owner, 2026-11-05: "some cars
    // wheels poke through the body work" - a Zonda, an Audi). Every model
    // has its body underside at y = 0 and its hubs at their resting height
    // (California 0.219 m against tire radius 0.339 - ride height 0.110 =
    // 0.229; R8 V10 0.168 against 0.174; -loghubs) [DATA]. Lifting only the
    // D_RIM_ nodes to the tire radius put each wheel about one ride height
    // (0.11 - 0.30 m) too high in its arch. The physics still wants its hub at
    // the tire radius above the contact: that is now the model lift of the
    // body frame (getBodyLift), the drawn car keeps its authored shape.
    // -wheellift restores the old wheel-only lift for an A/B.
    //
    // The body's height above the road is the car's static ride height,
    // DBCarPhysicsData _ride_height_front / _rear (mm, the mean of the two)
    // [DATA]; the hubs then go to tire radius - that height, which is a few
    // cm above the authored ones (the models hang their wheels a little below
    // the resting place: S3 2.7 cm, California 1.0 cm, R8 V10 0.6 cm). A ride
    // height outside 3..40 cm, or a wheel that would move more than 3 cm down
    // or 12 cm up, keeps the authored hubs (body lift = tire radius - hub).
    static const bool bWheelLift = CmdLine::HasArg( "-wheellift" );
    bodyLift = 0.0f;
    if ( !bWheelLift && !wheelNodes.empty() ) {
        float hubSum = 0.0f, hubMin = 1e9f, hubMax = -1e9f;
        for ( int32_t node : wheelNodes ) {
            const float hubY = car.getNodeTransform( node ).getTranslation().y;
            hubSum += hubY;
            hubMin = Min( hubMin, hubY );
            hubMax = Max( hubMax, hubY );
        }
        const float authoredLift = tireRadius - hubSum / static_cast<float>( wheelNodes.size() );
        float rideHeight = -1.0f;
        if ( gpDatabase != nullptr ) {
            XmbValue root = gpDatabase->getDocument().getRoot();
            for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
                XmbValue row = root.getArrayElement( "DBCarPhysicsData", i );
                if ( static_cast<int32_t>( row["_id_car"].asUInt64() ) == getIdCar() ) {
                    rideHeight = 0.0005f * static_cast<float>( row["_ride_height_front"].asInt() +
                                                               row["_ride_height_rear"].asInt() );
                    break;
                }
            }
        }
        const float hubTarget = tireRadius - rideHeight;
        const bool bRideHeight = rideHeight >= 0.03f && rideHeight <= 0.40f && hubTarget - hubMax >= -0.03f &&
                                 hubTarget - hubMin <= 0.12f;
        bodyLift = bRideHeight ? rideHeight : authoredLift;
        OTDU_LOG_INFO( "CinematicCar: '%s' body lift %.3f m (%s; authored hubs %.3f, tire radius %.3f)\n",
                       fileName.c_str(), bodyLift, bRideHeight ? "the DB ride height" : "the authored hubs",
                       hubSum / static_cast<float>( wheelNodes.size() ), tireRadius );
    }
    if ( CmdLine::HasArg( "-loghubs" ) && !wheelNodes.empty() ) {
        // The body's lowest point (model space, LOD 0, no wheel / tyre / brake
        // nodes) against the DB ride heights: where the ground is in the model.
        float bodyMinY = 1e9f, bodyMinFront = 1e9f, bodyMinRear = 1e9f;
        std::string minNode;
        for ( const EdenModel::Item &item : car.getItems() ) {
            if ( item.Lod != 0 || item.NodeIndex < 0 || item.NodeIndex >= static_cast<int32_t>( nodes.size() ) ) {
                continue;
            }
            const std::string &name = nodes[item.NodeIndex].Name;
            if ( name.find( '?' ) != std::string::npos || name.find( "RIM" ) != std::string::npos ||
                 name.find( "TIRE" ) != std::string::npos ||
                 name.rfind( "T_", 0 ) == 0 || name.find( "BRAKE" ) != std::string::npos ||
                 name.find( "STIR" ) != std::string::npos || name.find( "DISC" ) != std::string::npos ||
                 name.find( "FORK" ) != std::string::npos || name.find( "WHEEL" ) != std::string::npos ||
                 name.find( "SBOX" ) != std::string::npos || name.find( "SHADOW" ) != std::string::npos ) {
                continue;
            }
            const Matrix4x4f &m = car.getNodeTransform( item.NodeIndex );
            for ( int32_t corner = 0; corner < 8; corner++ ) {
                const Vector3f local( ( corner & 1 ) ? item.BoundsMax.x : item.BoundsMin.x,
                                      ( corner & 2 ) ? item.BoundsMax.y : item.BoundsMin.y,
                                      ( corner & 4 ) ? item.BoundsMax.z : item.BoundsMin.z );
                const Vector3f p = m.transformPoint( local );
                if ( p.y < bodyMinY ) {
                    bodyMinY = p.y;
                    minNode = name;
                }
                ( p.z > 0.0f ? bodyMinFront : bodyMinRear ) = Min( p.z > 0.0f ? bodyMinFront : bodyMinRear, p.y );
            }
        }
        float rhFront = 0.0f, rhRear = 0.0f;
        if ( gpDatabase != nullptr ) {
            XmbValue root = gpDatabase->getDocument().getRoot();
            for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
                XmbValue row = root.getArrayElement( "DBCarPhysicsData", i );
                if ( static_cast<int32_t>( row["_id_car"].asUInt64() ) == getIdCar() ) {
                    rhFront = static_cast<float>( row["_ride_height_front"].asInt() ) / 1000.0f;
                    rhRear = static_cast<float>( row["_ride_height_rear"].asInt() ) / 1000.0f;
                    break;
                }
            }
        }
        for ( int32_t node : wheelNodes ) {
            const Vector3f hub = car.getNodeTransform( node ).getTranslation();
            OTDU_LOG_INFO( "HUBS %s %s hub (%.3f %.3f %.3f) r %.3f | body min %.3f (front %.3f rear %.3f) | rh %.3f / %.3f | min node %s\n",
                           fileName.c_str(), nodes[node].Name.c_str(), hub.x, hub.y, hub.z, tireRadius, bodyMinY,
                           bodyMinFront, bodyMinRear, rhFront, rhRear, minNode.c_str() );
        }
    }
    for ( int32_t node : wheelNodes ) {
        const float hubY = car.getNodeTransform( node ).getTranslation().y;
        // To tire radius - body lift (the whole tire radius with -wheellift).
        const float lift = tireRadius - bodyLift - hubY;
        if ( std::abs( lift ) > 0.002f ) {
            const Matrix4x4f up = Matrix4x4f::Translation( Vector3f( 0.0f, lift, 0.0f ) );
            car.setNodeTransform( node, up * car.getNodeTransform( node ) );
            if ( node + 1 < static_cast<int32_t>( nodes.size() ) ) {
                car.setNodeTransform( node + 1, up * car.getNodeTransform( node + 1 ) );
            }
            OTDU_LOG_INFO( "CinematicCar: wheel '%s' hub lifted %.3f m (tire radius %.3f, body lift %.3f)\n",
                           nodes[node].Name.c_str(), lift, tireRadius, bodyLift );
            // The corner's brake nodes: BRAKE_xx is the rotor (a sibling of the
            // rim under DISC_xx), STIR_xx the caliper (a sibling of DISC_xx
            // under the upright). Both carry the corner's own suffix
            // ("D_RIM_FL" -> "BRAKE_FL" / "STIR_FL"), which is what pairs them
            // on every car model rather than walking a hierarchy that bikes and
            // some cars do not share. Exact names only: a "BRAKE_LIGHT" style
            // node must never take a wheel lift.
            const std::string &rimName = nodes[node].Name;
            const size_t cut = rimName.rfind( '_' );
            const std::string corner = cut == std::string::npos ? std::string() : rimName.substr( cut + 1 );
            if ( corner.empty() ) {
                continue;
            }
            const std::string brakeNames[2] = { "BRAKE_" + corner, "STIR_" + corner };
            for ( size_t i = 0; i < nodes.size(); i++ ) {
                if ( nodes[i].Name != brakeNames[0] && nodes[i].Name != brakeNames[1] ) {
                    continue;
                }
                car.setNodeTransform( static_cast<int32_t>( i ),
                                      up * car.getNodeTransform( static_cast<int32_t>( i ) ) );
                OTDU_LOG_INFO( "CinematicCar: brake node '%s' lifted %.3f m with the wheel '%s'\n",
                               nodes[i].Name.c_str(), lift, rimName.c_str() );
            }
        }
    }
    setRimDiameters( rimDiameterFront, rimDiameterRear );

    // The light driver materials / nodes, and the interior dashboard
    // materials (the night backlight): the ILUMCOLOR they ship with is
    // restored while the backlight is off.
    {
        collectLights();
        collectWipers();
        for ( DashMaterial &entry : dash ) {
            entry = DashMaterial();
        }
        gpsMaterials.clear();
        if ( interior.isLoaded() ) {
            static const char *const kDashNames[2] = { "HUD", "HUD_01" };
            const std::vector<EdenMaterial> &interiorMaterials = interior.getMaterials();
            for ( int32_t i = 0; i < 2; i++ ) {
                for ( size_t m = 0; m < interiorMaterials.size(); m++ ) {
                    if ( interiorMaterials[m].Name != kDashNames[i] ) {
                        continue;
                    }
                    dash[i].Index = static_cast<int32_t>( m );
                    auto it = interiorMaterials[m].Parameters.find( GetIdentifier64bit( "ILUMCOLOR" ) );
                    if ( it != interiorMaterials[m].Parameters.end() ) {
                        dash[i].RestIlum = it->second;
                    }
                    auto intensity =
                        interiorMaterials[m].Parameters.find( GetIdentifier64bit( "HEADLIGHTSINTENSITY" ) );
                    if ( intensity != interiorMaterials[m].Parameters.end() ) {
                        dash[i].RestIntensity = intensity->second;
                    }
                    break;
                }
            }
            // The sat-nav materials (TD_GPS shader): setGpsTexture binds the
            // map render target into their satellitemap slot.
            for ( size_t m = 0; m < interiorMaterials.size(); m++ ) {
                if ( interiorMaterials[m].ShaderSetGUID == kGpsShaderGuid ) {
                    gpsMaterials.push_back( static_cast<int32_t>( m ) );
                }
            }
        }
        OTDU_LOG_INFO( "CinematicCar: %zu light materials, %d dashboard materials (lights)\n", lightMaterials.size(),
                       ( dash[0].Index >= 0 ? 1 : 0 ) + ( dash[1].Index >= 0 ? 1 : 0 ) );
    }

    // -logcarlights: the lamp / dashboard / satnav materials and the shader
    // constants their programs declare. The light driver has to know which
    // constant (or texture slot) switches a lamp on, and whether the car
    // shaders even read an emissive constant.
    if ( CmdLine::HasArg( "-logcarlights" ) ) {
        for ( EdenModel *pModel : { &car, &interior } ) {
            if ( !pModel->isLoaded() ) {
                continue;
            }
            for ( const EdenMaterial &material : pModel->getMaterials() ) {
                const bool bLamp = material.Name.compare( 0, 4, "LGT_" ) == 0 || material.Name == "headlight" ||
                                   material.Name == "Lamp" || material.Name == "HUD" ||
                                   material.Name == "HUD_01" || material.Name == "GPS" ||
                                   material.Name == "Glass_LGT" || material.Name == "Glass_SK_LGT";
                if ( !bLamp ) {
                    continue;
                }
                std::string slots;
                for ( uint32_t i = 0; i < material.NumSlots; i++ ) {
                    slots += " [" + std::to_string( i ) + "]" +
                             ( material.pTextures[i] != nullptr ? material.pTextures[i]->Name : "NONE" );
                }
                OTDU_LOG_INFO( "CarLight: %s '%s' prog %s emissive (%.2f %.2f %.2f %.2f) slots%s\n",
                               pModel == &car ? "ext" : "int", material.Name.c_str(),
                               material.pProgram != nullptr ? material.pProgram->Name.c_str() : "-",
                               material.Emissive.x, material.Emissive.y, material.Emissive.z, material.Emissive.w,
                               slots.c_str() );
                if ( material.pProgram == nullptr ) {
                    continue;
                }
                for ( const ShaderConstantMember &m : material.pProgram->PSMembers ) {
                    OTDU_LOG_INFO( "  PS %s (%s) reg %u set %u\n", m.Name.c_str(), m.StructName.c_str(), m.Register,
                                   m.RegisterSet );
                }
                for ( const ShaderConstantMember &m : material.pProgram->VSMembers ) {
                    OTDU_LOG_INFO( "  VS %s (%s) reg %u set %u\n", m.Name.c_str(), m.StructName.c_str(), m.Register,
                                   m.RegisterSet );
                }
                for ( const auto &parameter : material.Parameters ) {
                    OTDU_LOG_INFO( "  param %s = %.3f %.3f %.3f %.3f\n",
                                   IdentifierToString( parameter.first ).c_str(), parameter.second.x,
                                   parameter.second.y, parameter.second.z, parameter.second.w );
                }
                // The shader constants that name a material parameter: a
                // missing one is written as zero (TD_DashBoard reads
                // HeadlightsIntensity / IlumParam / IlumColor).
                for ( const char *pCandidate : { "ILUMCOLOR", "ILUMPARAM", "HEADLIGHTSINTENSITY", "MATPARAM" } ) {
                    const uint64_t id = GetIdentifier64bit( pCandidate );
                    auto it = material.Parameters.find( id );
                    OTDU_LOG_INFO( "  has %s (%016llx): %s", pCandidate,
                                   static_cast<unsigned long long>( id ),
                                   it != material.Parameters.end() ? "yes" : "no" );
                    if ( it != material.Parameters.end() ) {
                        OTDU_LOG_INFO( " = %.3f %.3f %.3f %.3f", it->second.x, it->second.y, it->second.z,
                                       it->second.w );
                    }
                    OTDU_LOG_INFO( "\n" );
                }
            }
        }
    }

    // -logcaritems [<node name part>]: every item of the car model - its node,
    // LOD, material, shader, blend and vertex count (which part of a body does
    // not draw, and why).
    if ( CmdLine::HasArg( "-logcaritems" ) ) {
        const char *pFilter = CmdLine::GetArgValue( "-logcaritems" );
        const std::vector<SceneNode> &itemNodes = car.getScene().getNodes();
        std::vector<EdenMaterial> &materials = car.getMaterials();
        const std::vector<EdenModel::Item> &items = car.getItems();
        for ( size_t i = 0; i < items.size(); i++ ) {
            const EdenModel::Item &item = items[i];
            const std::string node = item.NodeIndex >= 0 && item.NodeIndex < static_cast<int32_t>( itemNodes.size() )
                                         ? itemNodes[item.NodeIndex].Name
                                         : std::string( "-" );
            if ( pFilter != nullptr && pFilter[0] != '-' && node.find( pFilter ) == std::string::npos ) {
                continue;
            }
            const EdenMaterial *pMaterial =
                item.MaterialIndex < materials.size() ? &materials[item.MaterialIndex] : nullptr;
            OTDU_LOG_INFO( "CARITEM %s #%zu node %d '%s' lod %u verts %u bones %zu mat '%s' shader 0x%llx blend %d alpha %d "
                           "visible %d box (%.2f %.2f %.2f)-(%.2f %.2f %.2f)\n",
                           fileName.c_str(), i, item.NodeIndex, node.c_str(), item.Lod, item.NumVertices,
                           item.BonePalette.size(), pMaterial != nullptr ? pMaterial->Name.c_str() : "?",
                           static_cast<unsigned long long>( pMaterial != nullptr ? pMaterial->ShaderSetGUID : 0 ),
                           pMaterial != nullptr && pMaterial->isBlended() ? 1 : 0,
                           pMaterial != nullptr && pMaterial->bAlphaTest ? 1 : 0,
                           item.NodeIndex >= 0 ? ( car.isNodeVisible( item.NodeIndex ) ? 1 : 0 ) : -1, item.BoundsMin.x,
                           item.BoundsMin.y, item.BoundsMin.z, item.BoundsMax.x, item.BoundsMax.y, item.BoundsMax.z );
        }
    }

    if ( CmdLine::HasArg( "-dumpcar" ) ) {
        for ( EdenModel *pModel : { &car, &interior } ) {
            if ( !pModel->isLoaded() ) {
                continue;
            }
            const std::vector<SceneNode> &dumpNodes = pModel->getScene().getNodes();
            for ( size_t i = 0; i < dumpNodes.size(); i++ ) {
                const Vector3f t = dumpNodes[i].World.getTranslation();
                const Matrix4x4f &l = dumpNodes[i].Local;
                OTDU_LOG_INFO( "DUMP %s node %zu parent %d '%s' obj %d t (%.3f %.3f %.3f) local [%.3f %.3f %.3f | %.3f "
                               "%.3f %.3f | %.3f %.3f %.3f | %.3f %.3f %.3f]\n",
                               pModel == &car ? "ext" : "int", i, dumpNodes[i].Parent, dumpNodes[i].Name.c_str(),
                               dumpNodes[i].ObjectHashcode != 0, t.x, t.y, t.z, l.m[0][0], l.m[0][1], l.m[0][2],
                               l.m[1][0], l.m[1][1], l.m[1][2], l.m[2][0], l.m[2][1], l.m[2][2], l.m[3][0], l.m[3][1],
                               l.m[3][2] );
            }
            for ( const EdenModel::Item &item : pModel->getItems() ) {
                const EdenMaterial &material = pModel->getMaterials()[item.MaterialIndex];
                std::string palette;
                for ( uint32_t bone : item.BonePalette ) {
                    palette += std::to_string( bone ) + ",";
                }
                OTDU_LOG_INFO( "DUMP %s item node %d '%s' mat '%s' prog %s cull %d blend %d/%d at %d bounds (%.3f %.3f "
                               "%.3f)-(%.3f %.3f %.3f) pal [%s]\n",
                               pModel == &car ? "ext" : "int", item.NodeIndex,
                               pModel->getScene().getNodes()[item.NodeIndex].Name.c_str(), material.Name.c_str(),
                               material.pProgram != nullptr ? material.pProgram->Name.c_str() : "-", material.CullMode,
                               material.SrcBlend, material.DstBlend, material.bAlphaTest, item.BoundsMin.x,
                               item.BoundsMin.y, item.BoundsMin.z, item.BoundsMax.x, item.BoundsMax.y,
                               item.BoundsMax.z, palette.c_str() );
            }
        }
    }
    if ( Logger::IsVerbose() ) {
        for ( const EdenModel::Item &item : car.getItems() ) {
            if ( car.getMaterials()[item.MaterialIndex].ShaderSetGUID != 0x132435465768798aull ) {
                continue;
            }
            const Matrix4x4f &m = car.getNodeTransform( item.NodeIndex );
            const float det = m.m[0][0] * ( m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1] ) -
                              m.m[0][1] * ( m.m[1][0] * m.m[2][2] - m.m[1][2] * m.m[2][0] ) +
                              m.m[0][2] * ( m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0] );
            OTDU_LOG_DEBUG( "  det %.3f\n", det );
            const Vector3f a = m.transformPoint( item.BoundsMin ), b = m.transformPoint( item.BoundsMax );
            OTDU_LOG_DEBUG( "shadow box node %d '%s' car space (%.3f %.3f %.3f)-(%.3f %.3f %.3f)\n", item.NodeIndex,
                            car.getScene().getNodes()[item.NodeIndex].Name.c_str(), a.x, a.y, a.z, b.x, b.y, b.z );
        }
        for ( const EdenModel::Item &item : rim.getItems() ) {
            const SceneNode &node = rim.getScene().getNodes()[item.NodeIndex];
            const Vector3f t = node.World.getTranslation();
            OTDU_LOG_DEBUG( "rim item node %d '%s' (%.3f %.3f %.3f) scale %.2f bounds (%.3f %.3f %.3f)-(%.3f %.3f %.3f) '%s'\n",
                            item.NodeIndex, node.Name.c_str(), t.x, t.y, t.z, node.World.m[1][1], item.BoundsMin.x,
                            item.BoundsMin.y, item.BoundsMin.z, item.BoundsMax.x, item.BoundsMax.y, item.BoundsMax.z,
                            rim.getMaterials()[item.MaterialIndex].Name.c_str() );
        }
        for ( EdenModel *pModel : { &car, &rim, &tire } ) {
            for ( const EdenMaterial &material : pModel->getMaterials() ) {
                std::string states;
                for ( uint32_t i = 0; i < 16; i++ ) {
                    char buffer[4];
                    snprintf( buffer, sizeof( buffer ), "%02x", material.States[i] );
                    states += buffer;
                }
                OTDU_LOG_DEBUG( "car material '%s' -> %s states %s alphatest %d cull %d blend %d/%d\n",
                                material.Name.c_str(),
                                material.pProgram != nullptr ? material.pProgram->Name.c_str() : "(none)",
                                states.c_str(), material.bAlphaTest, material.CullMode, material.SrcBlend,
                                material.DstBlend );
                for ( uint32_t i = 0; i < material.NumSlots; i++ ) {
                    OTDU_LOG_DEBUG( "  '%s' slot %u %s = %s (%s)\n", material.Name.c_str(), i,
                                    IdentifierToString( material.SlotTypes[i] ).c_str(),
                                    IdentifierToString( material.TextureHashes[i] ).c_str(),
                                    material.pTextures[i] != nullptr ? "ok" : "missing" );
                }
                for ( const auto &parameter : material.Parameters ) {
                    OTDU_LOG_DEBUG( "  '%s' param %s = %.3f %.3f %.3f %.3f\n", material.Name.c_str(),
                                    IdentifierToString( parameter.first ).c_str(), parameter.second.x,
                                    parameter.second.y, parameter.second.z, parameter.second.w );
                }
            }
        }
    }
    return true;
}

void CinematicCar::setRimDiameters( int32_t frontInch, int32_t rearInch ) {
    rimDiameterFront = frontInch > 0 ? frontInch : rimDiameterFront;
    rimDiameterRear = rearInch > 0 ? rearInch : rimDiameterFront;
    if ( tire.isLoaded() ) {
        tireNodeFront = tire.getScene().findNode( ( "T_" + std::to_string( rimDiameterFront ) + "P" ).c_str() );
        tireNodeRear = tire.getScene().findNode( ( "T_" + std::to_string( rimDiameterRear ) + "P" ).c_str() );
    }
    OTDU_LOG_INFO( "CinematicCar: rims %d\" / %d\", tire nodes %d / %d, tire radius %.3f m\n", rimDiameterFront,
                   rimDiameterRear, tireNodeFront, tireNodeRear, tireRadius );
}

void CinematicCar::setDashboardNeedles( float speedDegrees, float rpmDegrees ) {
    if ( !interior.isLoaded() ) {
        return;
    }
    // The needle meshes lie in the dial plane and rotate about their own local
    // Z (the models of the California and the R8 both sweep clockwise for a
    // positive angle; -needletest <node>:z:<deg> calibrated it). The rest
    // transform is the value-0 pose captured at load [INF: the retail code
    // FUN_00c52240 uses the ND_x pose nodes; the port's EdenModel keeps world
    // transforms, so a pose parent cannot drive its child].
    const auto apply = [&]( int32_t node, const Matrix4x4f &rest, float degrees ) {
        if ( node < 0 ) {
            return;
        }
        const Quaternionf rotation = Quaternionf::FromAxisAngle( Vector3f( 0.0f, 0.0f, 1.0f ),
                                                                degrees * kPI / 180.0f );
        interior.setNodeTransform( node, rest * Matrix4x4f::FromQuaternion( rotation ) );
    };
    apply( dashboardSpeedNode, dashboardSpeedRest, speedDegrees );
    apply( dashboardRpmNode, dashboardRpmRest, rpmDegrees );
}

void CinematicCar::setWheelSpin( float radians ) {
    // gm_freeride::updateWheels' cinematic branch (C_FIRST_OUTRO): the D_RIM_xx
    // node and its tyre sibling (node + 1) rotate about +x of the car frame,
    // from the rest transforms, hubs pinned. The rims / tyres are separate
    // models drawn at these node transforms (draw()).
    if ( !car.isLoaded() || wheelNodes.empty() ) {
        return;
    }
    const auto withoutTranslation = []( Matrix4x4f m ) {
        m.m[3][0] = m.m[3][1] = m.m[3][2] = 0.0f;
        return m;
    };
    if ( !bWheelSpinRests ) {
        for ( size_t i = 0; i < wheelNodes.size(); i++ ) {
            wheelSpinRests[0].push_back( car.getNodeTransform( wheelNodes[i] ) );
            wheelSpinRests[1].push_back( car.getNodeTransform( wheelNodes[i] + 1 ) );
        }
        bWheelSpinRests = true;
    }
    const Matrix4x4f motion =
        Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 1.0f, 0.0f, 0.0f ), radians ) );
    for ( size_t i = 0; i < wheelNodes.size(); i++ ) {
        const Matrix4x4f &rest = wheelSpinRests[0][i];
        const Matrix4x4f &tireRest = wheelSpinRests[1][i];
        car.setNodeTransform( wheelNodes[i],
                              Matrix4x4f::Translation( rest.getTranslation() ) * motion * withoutTranslation( rest ) );
        car.setNodeTransform( wheelNodes[i] + 1,
                              Matrix4x4f::Translation( tireRest.getTranslation() ) * motion *
                                  withoutTranslation( tireRest ) );
    }
}

void CinematicCar::setWindowsOpen( bool bOpen ) {
    // The retail switch is a factor 0..1 (FUN_00c66150(f)); the caller only
    // requests the state, updateWindows slides the glass there.
    bWindowsOpen = bOpen;
    windowTarget = bOpen ? 1.0f : 0.0f;
}

void CinematicCar::updateWindows( float deltaTime ) {
    if ( windowTime == windowTarget ) {
        return;
    }
    // FUN_00c66150 / FUN_00c66110 take the 0..1 factor; the frame to frame
    // duration is not in the readable code [INF: 1.2 s, the hardtop's own
    // clip is 17 s for a much larger move].
    const float step = deltaTime / kWndAnimDuration;
    windowTime = windowTarget > windowTime ? Min( windowTime + step, windowTarget )
                                          : Max( windowTime - step, windowTarget );
    for ( int32_t side = 0; side < 2; side++ ) {
        const WindowNodes &window = windows[side];
        if ( window.Raised < 0 ) {
            continue;
        }
        // FUN_00c66150: the glass moves onto the WND_x2 pose (the door frame
        // at the bottom of the glass) when it is lowered, back to its own rest
        // when raised.
        car.setNodeTransform( window.Raised,
                              BlendRigid( window.RaisedRest, window.LoweredRest, windowTime ) );
    }
}

void CinematicCar::setHeadlights( bool bOn ) {
    if ( bHeadlights == bOn ) {
        return;
    }
    bHeadlights = bOn;
    OTDU_LOG_INFO( "CinematicCar: headlights %s\n", bOn ? "on" : "off" );
}

void CinematicCar::setIndicator( int32_t side ) {
    side = side < 0 ? -1 : ( side & 1 );
    if ( indicatorSide == side ) {
        return;
    }
    indicatorSide = side;
    indicatorTimer = side >= 0 ? 0.0f : -1.0f; // lit at once when it starts
    OTDU_LOG_INFO( "CinematicCar: indicator %s\n",
                   side < 0 ? "off" : ( side == 0 ? "left" : "right" ) );
}

void CinematicCar::setBrakeLights( bool bOn ) {
    bBraking = bOn;
}

void CinematicCar::setReverseLights( bool bOn ) {
    bReverse = bOn;
}

// The blinker timer (FUN_00c57ef0 [CODE]): the timer is -1 while no blinker
// is on, 0 when one starts (the lamp lights at once), then t = fmod(t + dt,
// 1.0) with the lamps lit while t < 0.6. -indicator left|right|hazard forces
// the state; -carlights 0|1|2 forces the light mode (FUN_00c588d0's +0x1d4:
// 0 everything off, 1 the weather keys, 2 headlights forced).
void CinematicCar::updateLights( float deltaTime ) {
    static const char *pForcedIndicator = CmdLine::GetArgValue( "-indicator" );
    int32_t side = indicatorSide;
    if ( pForcedIndicator != nullptr ) {
        if ( StringCmpI( pForcedIndicator, "left" ) == 0 ) {
            side = 0;
        } else if ( StringCmpI( pForcedIndicator, "right" ) == 0 ) {
            side = 1;
        } else if ( StringCmpI( pForcedIndicator, "hazard" ) == 0 ) {
            side = 2; // both sides
        } else {
            side = -1;
        }
    }
    const bool bLeftBlink = side == 0 || side == 2;
    const bool bRightBlink = side == 1 || side == 2;
    if ( indicatorTimer < 0.0f ) {
        if ( bLeftBlink || bRightBlink ) {
            indicatorTimer = 0.0f;
        }
    } else if ( !bLeftBlink && !bRightBlink ) {
        indicatorTimer = -1.0f;
    }
    float left = 0.0f, right = 0.0f;
    if ( indicatorTimer >= 0.0f ) {
        indicatorTimer = std::fmod( indicatorTimer + deltaTime, kBlinkerPeriod );
        if ( indicatorTimer < kBlinkerOnTime ) {
            left = bLeftBlink ? 1.0f : 0.0f;
            right = bRightBlink ? 1.0f : 0.0f;
        }
    }
    // The lamp rises in ~35 ms and dies in ~110 ms (an incandescent bulb, not
    // a switch: the hard 0 / 1 gate read as a flicker) [INF]; the retail
    // cadence above is untouched - the lamp is lit for the same 0.6 s.
    const float targets[2] = { left, right };
    for ( int32_t i = 0; i < 2; i++ ) {
        const float rate = targets[i] > indicatorGlow[i] ? 1.0f / 0.035f : 1.0f / 0.11f;
        const float step = Min( 1.0f, deltaTime * rate );
        indicatorGlow[i] += ( targets[i] - indicatorGlow[i] ) * step;
        if ( indicatorGlow[i] < 0.002f ) {
            indicatorGlow[i] = 0.0f;
        }
        indicatorValue[i] = indicatorGlow[i];
    }
    bIndicatorLamp = left > 0.0f || right > 0.0f;
    // High beam delay (mode 2: the position light function takes the flare
    // value after 0.2 s, 0x00f49b58; reset in the other modes).
    static const int32_t forcedLights = CmdLine::GetArgInt( "-carlights", -2 );
    if ( forcedLights == 0 ) {
        headlightMode = 0;
    } else if ( forcedLights > 0 ) {
        headlightMode = 2;
    } else {
        headlightMode = bHeadlights ? 2 : 1;
    }
    if ( headlightMode == 2 ) {
        highBeamTimer += deltaTime;
    } else {
        highBeamTimer = 0.0f;
    }
}

// The light materials (FUN_00c5ee20), the lamp glow nodes (FUN_00c5f060) and
// the projected light boxes (car slots 13 / 14 / 15).
void CinematicCar::collectLights() {
    lightMaterials.clear();
    causticNodes.clear();
    flareNodes.clear();
    for ( int32_t i = 0; i < 3; i++ ) {
        lightBoxNodes[i] = -1;
        lightBoxMaterials[i].clear();
    }
    if ( !car.isLoaded() ) {
        return;
    }
    const std::vector<EdenMaterial> &materials = car.getMaterials();
    static const uint64_t kGlossFct = GetIdentifier64bit( "GLOSSFCT" );
    for ( uint32_t m = 0; m < materials.size(); m++ ) {
        const uint64_t hashcode = GetIdentifier64bit( materials[m].Name.c_str() );
        for ( const LightMaterialName &entry : kLightMaterials ) {
            const uint64_t nameHash = GetIdentifier64bit( entry.pName );
            if ( nameHash == hashcode && materials[m].Parameters.count( kGlossFct ) != 0 ) {
                lightMaterials.push_back( { m, entry.bLeft, entry.bFront } );
                break;
            }
        }
    }
    const std::vector<SceneNode> &nodes = car.getScene().getNodes();
    // Lamp glow nodes: hashcodes of "Fa_<c>_<FL|FR|RL|RR>" (and the same with
    // a 1..5 index, and each with "_C" appended) for the functions "LBSWR"
    // (string 0x00ef5fb8 [CODE]); the California only has the "_C" meshes.
    static const char *const kPositions[4] = { "FL", "FR", "RL", "RR" };
    std::unordered_map<uint64_t, LightNode> lampNames;
    for ( const char *pType = "LBSWR"; *pType != '\0'; pType++ ) {
        for ( int32_t p = 0; p < 4; p++ ) {
            for ( int32_t n = 0; n < 6; n++ ) {
                char name[32];
                if ( n == 0 ) {
                    snprintf( name, sizeof( name ), "Fa_%c_%s", *pType, kPositions[p] );
                } else {
                    snprintf( name, sizeof( name ), "Fa_%c_%s%d", *pType, kPositions[p], n );
                }
                LightNode light;
                light.Function = *pType;
                light.bLeft = kPositions[p][1] == 'L';
                light.bFront = kPositions[p][0] == 'F';
                light.bFlare = true;
                lampNames[GetIdentifier64bit( name )] = light;
                strncat( name, "_C", sizeof( name ) - strlen( name ) - 1 );
                light.bFlare = false;
                lampNames[GetIdentifier64bit( name )] = light;
            }
        }
    }
    for ( size_t i = 0; i < nodes.size(); i++ ) {
        auto lamp = lampNames.find( nodes[i].Hashcode );
        if ( lamp != lampNames.end() ) {
            LightNode light = lamp->second;
            light.Node = static_cast<int32_t>( i );
            ( light.bFlare ? flareNodes : causticNodes ).push_back( light );
        }
        const int32_t box = strstr( nodes[i].Name.c_str(), "BREAKI" ) != nullptr ? 0
                            : strstr( nodes[i].Name.c_str(), "EVERS" ) != nullptr ? 1
                            : strstr( nodes[i].Name.c_str(), "EADLI" ) != nullptr ? 2
                                                                                  : -1;
        if ( box >= 0 && nodes[i].Parent == 0 ) {
            lightBoxNodes[box] = static_cast<int32_t>( i );
        }
    }
    static const uint64_t kIntensity = GetIdentifier64bit( "INTENSITY" );
    for ( const EdenModel::Item &item : car.getItems() ) {
        for ( int32_t box = 0; box < 3; box++ ) {
            if ( item.NodeIndex == lightBoxNodes[box] &&
                 materials[item.MaterialIndex].Parameters.count( kIntensity ) != 0 ) {
                lightBoxMaterials[box].push_back( item.MaterialIndex );
            }
        }
    }
    if ( CmdLine::HasArg( "-logcarlights" ) ) {
        for ( const LightNode &light : causticNodes ) {
            Vector3f boundsMin, boundsMax;
            const bool bBounds = car.getNodeBounds( light.Node, boundsMin, boundsMax );
            const Vector3f t = car.getNodeTransform( light.Node ).getTranslation();
            OTDU_LOG_INFO( "CarLight: glow node '%s' %c %s%s at (%.2f, %.2f, %.2f) bounds %d (%.2f, %.2f, %.2f) - (%.2f, %.2f, %.2f)\n",
                           nodes[light.Node].Name.c_str(), light.Function, light.bFront ? "front" : "rear",
                           light.bLeft ? " left" : " right", t.x, t.y, t.z, bBounds ? 1 : 0, boundsMin.x, boundsMin.y,
                           boundsMin.z, boundsMax.x, boundsMax.y, boundsMax.z );
        }
        for ( const EdenModel::Item &item : car.getItems() ) {
            for ( const LightMaterial &light : lightMaterials ) {
                if ( item.MaterialIndex == light.Material && item.Lod == 0 ) {
                    const Vector3f t = item.NodeIndex >= 0 ? car.getNodeTransform( item.NodeIndex ).getTranslation() : Vector3f();
                    OTDU_LOG_INFO( "CarLight: lamp item '%s' node '%s' (at %.2f, %.2f, %.2f) bounds (%.2f, %.2f, %.2f) - (%.2f, %.2f, %.2f)\n",
                                   materials[light.Material].Name.c_str(),
                                   item.NodeIndex >= 0 ? nodes[item.NodeIndex].Name.c_str() : "?", t.x, t.y, t.z, item.BoundsMin.x,
                                   item.BoundsMin.y, item.BoundsMin.z, item.BoundsMax.x, item.BoundsMax.y, item.BoundsMax.z );
                }
            }
        }
    }
    collectLampAnchors();
    OTDU_LOG_INFO( "CinematicCar: %zu light materials, %zu lamp glow nodes, %zu flare nodes, light boxes %d %d %d\n",
                   lightMaterials.size(), causticNodes.size(), flareNodes.size(), lightBoxNodes[0], lightBoxNodes[1],
                   lightBoxNodes[2] );
}

// Where the glows shine from (car_lights_spec.md 8) [INF: the retail bloom
// comes from the post chain, there is no anchor list in the data]:
//   - the lamp glow nodes ?A_<L|S|R|W|B>_<pos>_ (FUN_00c5f060's meshes): the
//     headlamps, the front blinkers, the stop and reverse lamps - the centre
//     of the mesh, in its node's space;
//   - the lamp items of the light materials, for what has no glow node: a
//     rear cluster on its own node (LGT_RL / LGT_RR) shines the position
//     light and the rear blinker, a wide strip on a body node at the rear is
//     the third stop light, a tiny lens on a body node at the front is a side
//     repeater. The mid-LOD copies (nodes named "?GT_MR_*") and the closed
//     roof's lamps (_SK, the hardtop node) are skipped.
void CinematicCar::collectLampAnchors() {
    lampAnchors.clear();
    if ( !car.isLoaded() ) {
        return;
    }
    const std::vector<SceneNode> &nodes = car.getScene().getNodes();
    const std::vector<EdenMaterial> &materials = car.getMaterials();
    auto addAnchor = [this]( const LampAnchor &anchor ) {
        // One glow per function and spot (the HR / MR body copies of a lens).
        for ( const LampAnchor &other : lampAnchors ) {
            if ( other.Function == anchor.Function && other.Node == anchor.Node &&
                 ( other.Local - anchor.Local ).length() < 0.05f ) {
                return;
            }
        }
        lampAnchors.push_back( anchor );
    };
    bool bHasFunction[128] = {};
    for ( const LightNode &light : causticNodes ) {
        Vector3f boundsMin, boundsMax;
        if ( !car.getNodeBounds( light.Node, boundsMin, boundsMax ) ) {
            continue;
        }
        LampAnchor anchor;
        anchor.Node = light.Node;
        anchor.Local = ( boundsMin + boundsMax ) * 0.5f;
        anchor.Function = light.Function;
        anchor.bLeft = light.bLeft;
        anchor.bFront = light.bFront;
        anchor.Normal = Vector3f( 0.0f, 0.0f, light.bFront ? 1.0f : -1.0f );
        const Vector3f extent = boundsMax - boundsMin;
        anchor.Size = Clamp( Max( extent.x, extent.y ) * 0.9f, 0.08f, 0.32f );
        addAnchor( anchor );
        bHasFunction[static_cast<uint8_t>( light.Function ) & 127] = true;
    }
    for ( const EdenModel::Item &item : car.getItems() ) {
        const LightMaterial *pLight = nullptr;
        for ( const LightMaterial &light : lightMaterials ) {
            if ( light.Material == item.MaterialIndex ) {
                pLight = &light;
                break;
            }
        }
        if ( pLight == nullptr || item.NodeIndex < 0 ) {
            continue;
        }
        const std::string &materialName = materials[pLight->Material].Name;
        const std::string &nodeName = nodes[item.NodeIndex].Name;
        if ( materialName.find( "_SK" ) != std::string::npos || nodeName.empty() || nodeName[0] == '?' ) {
            continue;
        }
        const Vector3f center = ( item.BoundsMin + item.BoundsMax ) * 0.5f;
        const Vector3f extent = item.BoundsMax - item.BoundsMin;
        // The item bounds are model space (the rear cluster's node carries a
        // pose that would drop its rings onto the stop lamps): no node.
        LampAnchor anchor;
        anchor.Node = -1;
        anchor.Local = center;
        anchor.bLeft = pLight->bLeft;
        anchor.bFront = pLight->bFront;
        anchor.Normal = Vector3f( 0.0f, 0.0f, pLight->bFront ? 1.0f : -1.0f );
        const bool bOwnNode = StringCmpI( nodeName.c_str(), materialName.c_str() ) == 0;
        const bool bBodyNode = StringCmpI( nodeName.substr( 0, 5 ).c_str(), "BODY_" ) == 0;
        if ( bOwnNode && !pLight->bFront ) {
            // The rear cluster: the position light ring and the blinker.
            anchor.Size = Clamp( Max( extent.x, extent.y ) * 0.55f, 0.08f, 0.22f );
            anchor.Function = 'B';
            addAnchor( anchor );
            // (The blinker glow nodes are the front ones: the rear blinker
            // is the cluster's amber centre.)
            anchor.Function = 'W';
            anchor.Size *= 0.85f;
            addAnchor( anchor );
            if ( !bHasFunction['S'] ) {
                anchor.Function = 'S';
                addAnchor( anchor );
            }
        } else if ( bOwnNode && pLight->bFront && !bHasFunction['L'] ) {
            anchor.Size = Clamp( Max( extent.x, extent.y ) * 0.8f, 0.10f, 0.30f );
            anchor.Function = 'L';
            addAnchor( anchor );
        } else if ( bBodyNode && !pLight->bFront && extent.x > 0.25f ) {
            // The third stop light: a strip, a band of overlapping halos.
            for ( float u : { -0.40f, -0.20f, 0.0f, 0.20f, 0.40f } ) {
                LampAnchor strip = anchor;
                strip.Local = center + Vector3f( u * extent.x, 0.0f, 0.0f );
                strip.Size = Clamp( extent.x * 0.20f, 0.05f, 0.12f );
                strip.Function = 'S';
                strip.bStrip = true;
                addAnchor( strip );
            }
        } else if ( bBodyNode && pLight->bFront && Max( extent.y, extent.z ) < 0.10f ) {
            // A side repeater: shines sideways.
            anchor.Size = 0.05f;
            anchor.Function = 'W';
            anchor.bRepeater = true;
            anchor.Normal = Vector3f( center.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f );
            addAnchor( anchor );
        }
    }
    if ( CmdLine::HasArg( "-logcarlights" ) ) {
        for ( const LampAnchor &anchor : lampAnchors ) {
            const Vector3f p = anchor.Node >= 0 ? car.getNodeTransform( anchor.Node ).transformPoint( anchor.Local ) : anchor.Local;
            OTDU_LOG_INFO( "CarLight: glow anchor %c %s %s%s at model (%.2f, %.2f, %.2f) size %.2f\n", anchor.Function,
                           anchor.bFront ? "front" : "rear", anchor.bLeft ? "left" : "right",
                           anchor.bRepeater ? " (repeater)" : "", p.x, p.y, p.z, anchor.Size );
        }
    }
}

void CinematicCar::appendLampGlows( const Matrix4x4f &world, std::vector<EdenLampGlow> &out ) const {
    if ( !car.isLoaded() ) {
        return;
    }
    // By day the sun washes a lamp's bloom out: the stop / blinker / reverse
    // keep part of it, the headlamps a little, the position lights none
    // [INF: by eye against the retail captures].
    const float night = appliedNight;
    for ( const LampAnchor &anchor : lampAnchors ) {
        float value = 0.0f, dayShare = 0.0f, core = 1.0f, spread = 0.22f, sizeScale = 1.0f;
        Vector3f color;
        switch ( anchor.Function ) {
        case 'L':
            // The front blinker sits in the headlamp cluster: its amber is
            // lost under the white glow unless that side's lamp gives way.
            value = appliedHeadlight * ( 1.0f - 0.5f * indicatorValue[anchor.bLeft ? 0 : 1] );
            color = Vector3f( 1.00f, 0.93f, 0.82f );
            dayShare = 0.30f;
            spread = 0.12f;
            break;
        case 'B':
            value = appliedPosition * 0.7f;
            color = Vector3f( 1.00f, 0.09f, 0.04f );
            dayShare = 0.0f;
            core = 0.0f;
            break;
        case 'S':
            value = appliedBrake * ( anchor.bStrip ? 0.7f : 1.0f );
            color = Vector3f( 1.00f, 0.06f, 0.03f );
            dayShare = 0.55f;
            core = anchor.bStrip ? 0.0f : 1.0f;
            break;
        case 'R':
            value = appliedReverse;
            color = Vector3f( 0.95f, 0.96f, 1.00f );
            dayShare = 0.50f;
            break;
        case 'W':
            value = indicatorValue[anchor.bLeft ? 0 : 1];
            color = Vector3f( 1.00f, 0.46f, 0.05f );
            dayShare = 0.70f;
            spread = anchor.bRepeater ? 0.5f : 0.25f;
            sizeScale = anchor.bFront && !anchor.bRepeater ? 1.25f : 1.0f;
            break;
        default:
            break;
        }
        const float strength = value * ( dayShare + ( 1.0f - dayShare ) * night );
        if ( strength <= 0.01f ) {
            continue;
        }
        const Matrix4x4f node = anchor.Node >= 0 ? world * car.getNodeTransform( anchor.Node ) : world;
        EdenLampGlow glow;
        glow.Position = node.transformPoint( anchor.Local );
        Vector3f normal = world.transformVector( anchor.Normal );
        glow.Normal = normal.length() > 1e-4f ? normal.normalized() : Vector3f( 0.0f, 0.0f, 1.0f );
        glow.Color = color;
        glow.Size = anchor.Size * sizeScale * ( 0.75f + 0.25f * night );
        glow.Strength = Saturate( strength );
        glow.Core = core;
        glow.Spread = spread;
        out.push_back( glow );
    }
}

// The per frame light state (FUN_00c588d0 + FUN_00c63a30, docs/re/
// car_lights_spec.md 2.2 / 2.5): the GLOSSFCT channels of every light
// material, the lamp glow node flags and the light box INTENSITY.
void CinematicCar::applyLights( const WeatherPreset *pWeather ) {
    auto weather = [pWeather, this]( const char *pName ) {
        // FUN_00c4e710: the car_* values, the tunnel_ variants in a tunnel.
        if ( pWeather == nullptr ) {
            return 0.0f;
        }
        return pWeather->getFloat( ( std::string( bInTunnel ? "tunnel_" : "" ) + pName ).c_str(), 0.0f );
    };
    const int32_t mode = headlightMode < 0 ? 1 : headlightMode;
    const float rawFlare = weather( "car_headlight_flare_intensity" );
    const float flare = rawFlare < 0.05f ? 0.0f : rawFlare;
    const float poslight = weather( "car_poslight_intensity" );
    float headlightValue = mode == 2 ? flare + 1.0f : mode == 1 ? flare : 0.0f;
    float positionValue = poslight < 0.05f ? 0.0f : poslight;
    if ( mode == 2 && highBeamTimer > 0.2f ) {
        positionValue = rawFlare;
    }
    if ( mode == 0 ) {
        headlightValue = positionValue = 0.0f;
    }
    const float brakeValue = bBraking ? 1.0f : 0.0f;
    const float reverseValue = bReverse ? 1.0f : 0.0f;
    // GLOSSFCT channels (the TEXLIGHT colour under each GLOSSLIGHT mask):
    // x brake (red), y reverse (white), z position / headlight (reflector +
    // rear ring), w the blinker of the material side (amber centres). The
    // front lamps take the headlights over the position light and carry no
    // brake / reverse.
    static const uint64_t kGlossFct = GetIdentifier64bit( "GLOSSFCT" );
    std::vector<EdenMaterial> &materials = car.getMaterials();
    // The emission multiplier over every lit channel (setLampBoost): the
    // channels are plain 0/1 gates and the lens read dimmer on screen than
    // the retail screenshots, so the boost pushes them over 1.
    const float boost = lampBoost;
    // Per function on top of it: the stop and the blinker lamps are the
    // brightest bulbs of a car (21 W against the 5 W tail light), and at 1.4
    // they still read as a matte red / a dull orange dot on the lens [INF].
    constexpr float kBrakeWeight = 1.45f, kBlinkerWeight = 1.6f, kReverseWeight = 1.2f;
    for ( const LightMaterial &light : lightMaterials ) {
        auto it = materials[light.Material].Parameters.find( kGlossFct );
        if ( it == materials[light.Material].Parameters.end() ) {
            continue;
        }
        it->second = Vector4f( light.bFront ? 0.0f : brakeValue * boost * kBrakeWeight,
                               light.bFront ? 0.0f : reverseValue * boost * kReverseWeight,
                               light.bFront ? Max( positionValue, headlightValue ) * boost : positionValue * boost,
                               indicatorValue[light.bLeft ? 0 : 1] * boost * kBlinkerWeight );
    }
    appliedHeadlight = Saturate( headlightValue );
    appliedPosition = Saturate( positionValue );
    appliedBrake = brakeValue;
    appliedReverse = reverseValue;
    appliedNight = Saturate( rawFlare );
    // The lamp glow meshes / flare sprites: shown with their function.
    for ( const std::vector<LightNode> *pNodes : { &causticNodes, &flareNodes } ) {
        for ( const LightNode &node : *pNodes ) {
            float value = 0.0f;
            switch ( node.Function ) {
            case 'L': value = headlightValue; break;
            case 'B': value = positionValue; break;
            case 'S': value = brakeValue; break;
            case 'R': value = reverseValue; break;
            case 'W': value = indicatorValue[node.bLeft ? 0 : 1] > 0.5f ? 1.0f : 0.0f; break;
            default: break;
            }
            static const bool bNoFlares = CmdLine::HasArg( "-noflares" );
            car.setNodeVisible( node.Node, value > 0.0f && !( node.bFlare && bNoFlares ) );
        }
    }
    // The projected light boxes: v = car_caustics_intensity (0 in mode 0,
    // +1 in mode 2); BREAKING while braking, REVERSING in reverse, HEADLIGHT
    // while the lights are on (x0.3 for vehicle kind 1 [INF: not applied]).
    float caustics = weather( "car_caustics_intensity" );
    if ( mode == 0 ) {
        caustics = 0.0f;
    } else if ( mode == 2 ) {
        caustics += 1.0f;
    }
    static const bool bNoLightBoxes = CmdLine::HasArg( "-nolightboxes" );
    const bool bBoxOn[3] = { caustics > 0.05f && bBraking, caustics > 0.05f && bReverse, caustics > 0.0f };
    static const uint64_t kIntensity = GetIdentifier64bit( "INTENSITY" );
    for ( int32_t box = 0; box < 3; box++ ) {
        if ( lightBoxNodes[box] < 0 ) {
            continue;
        }
        car.setNodeVisible( lightBoxNodes[box], bBoxOn[box] && !bNoLightBoxes );
        for ( uint32_t m : lightBoxMaterials[box] ) {
            auto it = materials[m].Parameters.find( kIntensity );
            if ( it != materials[m].Parameters.end() ) {
                it->second = Vector4f( caustics, 0.0f, 0.0f, 0.0f );
            }
        }
    }
}

void CinematicCar::setDashboardLit( bool bOn ) {
    if ( bDashboardLit == bOn ) {
        return;
    }
    bDashboardLit = bOn;
    // TD_DashBoard reads IlumColor / IlumParam / HeadlightsIntensity. The
    // material ships HEADLIGHTSINTENSITY = 0, which zeroes the backlight term,
    // so the driver raises it with the headlights [INF: no readable caller].
    for ( int32_t i = 0; i < 2; i++ ) {
        if ( dash[i].Index < 0 ) {
            continue;
        }
        EdenMaterial &material = interior.getMaterials()[dash[i].Index];
        Vector4f ilum = dash[i].RestIlum;
        Vector4f intensity = dash[i].RestIntensity;
        if ( bOn ) {
            ilum = kDashLitColor;
            intensity = Vector4f( kDashLitIntensity, 0.0f, 0.0f, 0.0f );
        }
        material.Parameters[GetIdentifier64bit( "ILUMCOLOR" )] = ilum;
        material.Parameters[GetIdentifier64bit( "HEADLIGHTSINTENSITY" )] = intensity;
    }
    OTDU_LOG_INFO( "CinematicCar: dashboard %s\n", bOn ? "lit" : "dark" );
}

bool CinematicCar::setGpsTexture( EdenRenderer &renderer, GPUTexture *pTexture, const Vector4f &uvOffset ) {
    if ( gpsMaterials.empty() || !interior.isLoaded() || pTexture == nullptr ) {
        return false;
    }
    // The material's UVOFFSET parameter feeds the TD_GPS vertex shader's
    // UVOffset constant (reg 67, fillConstants by stripped name): TDU1's
    // setGPSTexture writes [0]=minimap atlas U, [1]=V, [2]=3.0. Our render
    // target is the whole map (no shared atlas), so U/V stay 1.
    static const uint64_t kUvOffset = GetIdentifier64bit( "UVOFFSET" );
    bool bAssigned = false;
    for ( int32_t index : gpsMaterials ) {
        EdenMaterial &material = interior.getMaterials()[index];
        // The GPS material's single texture slot ships the generic COLOR type
        // (the .2dm's satellitemap spelling is the texture's file name, not
        // the slot type); one material, so the first match is the screen.
        bAssigned |= renderer.assignTexture( material, "COLOR", pTexture );
        if ( !bAssigned ) {
            bAssigned |= renderer.assignTexture( material, "SATELLITEMAP", pTexture );
        }
        material.Parameters[kUvOffset] = uvOffset;
        renderer.updateProgram( material );
    }
    // The attempt is final either way (the caller must not re-render and
    // retry every frame when the material has no satellitemap slot).
    bGpsBound = true;
    if ( !bAssigned ) {
        const EdenMaterial &m = interior.getMaterials()[gpsMaterials[0]];
        OTDU_LOG_WARN( "CinematicCar: dash GPS bind failed, no satellitemap slot on %d material(s)"
                       " (slots: %d, types: '%s' %016llx)\n",
                       static_cast<int32_t>( gpsMaterials.size() ), m.NumSlots,
                       IdentifierToString( m.SlotTypes[0] ).c_str(),
                       ( unsigned long long )m.SlotTypes[0] );
        return false;
    }
    for ( int32_t node : gpsNodes ) {
        interior.setNodeVisible( node, true );
    }
    OTDU_LOG_INFO( "CinematicCar: dash GPS bound, %d material(s), %d node(s), uv (%.1f %.1f %.1f)\n",
                   static_cast<int32_t>( gpsMaterials.size() ), static_cast<int32_t>( gpsNodes.size() ), uvOffset.x,
                   uvOffset.y, uvOffset.z );
    return true;
}

void CinematicCar::setHardtopOpen( bool bOpen ) {
    if ( !bHardtop ) {
        return;
    }
    // FUN_00c3fcc0 / FUN_00c66920: the Hardtop.anm clip runs over its own
    // duration (17 s on the California); 0 is the closed roof.
    hardtopTarget = bOpen ? 0.99999f * hardtopAnimation.getDuration() : 0.0f;
}

void CinematicCar::updateHardtop( float deltaTime ) {
    if ( !bHardtop || hardtopTarget == hardtopTime ) {
        return;
    }
    hardtopTime = hardtopTarget > hardtopTime ? Min( hardtopTime + deltaTime, hardtopTarget )
                                             : Max( hardtopTime - deltaTime, hardtopTarget );
    std::vector<BoneTransform> locals( hardtopSkeleton.getNumBones() );
    for ( uint32_t bone = 0; bone < hardtopSkeleton.getNumBones(); bone++ ) {
        locals[bone] = hardtopSkeleton.getBindLocal( bone );
    }
    hardtopAnimation.sample( hardtopSkeleton, hardtopTime, false, locals );
    std::vector<Matrix4x4f> boneWorld;
    hardtopSkeleton.computeSkinMatrices( locals, boneWorld, hardtopSkin );
}

void CinematicCar::setDoorAngle( int32_t side, float angle ) {
    DoorNodes &door = doors[side & 1];
    doorAngle[side & 1] = angle;
    if ( door.Door < 0 ) {
        return;
    }
    // FUN_00ddbcd0(angle): rotation about the node Y axis (matrix entries
    // [0][0], [0][2], [2][0], [2][2]; the sin / cos helpers are encrypted
    // [ENC]); applied to the door node frame [INF: post-multiplied local]. A
    // negative angle swings the left door outwards (+X is the car's left).
    const Matrix4x4f rotation = Matrix4x4f::FromQuaternion(
        Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), angle ) );
    const Matrix4x4f delta = door.DoorRest * rotation * door.DoorRest.inverse();
    for ( const auto &rest : door.CarRest ) {
        car.setNodeTransform( rest.first, delta * rest.second );
    }
    for ( const auto &rest : door.InteriorRest ) {
        interior.setNodeTransform( rest.first, delta * rest.second );
    }
}

int32_t CinematicCar::getIdCar() const {
    if ( gpDatabase == nullptr || fileName.empty() ) {
        return -1;
    }
    XmbValue root = gpDatabase->getDocument().getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
        XmbValue record = root.getArrayElement( "DBCarPhysicsData", i );
        if ( StringCmpI( record["_car_file_name"].asString().c_str(), fileName.c_str() ) == 0 ) {
            return static_cast<int32_t>( record["_id_car"].asInt( -1 ) );
        }
    }
    return -1;
}

namespace {
// The DBCarsAndColors rows of a car, in table order (paint_idx indexes them,
// paint_spec.md 1) [DATA + INF].
std::vector<XmbValue> PaintRows( int32_t carId ) {
    std::vector<XmbValue> rows;
    if ( gpDatabase == nullptr || carId < 0 ) {
        return rows;
    }
    XmbValue root = gpDatabase->getDocument().getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( "DBCarsAndColors" ); i++ ) {
        XmbValue row = root.getArrayElement( "DBCarsAndColors", i );
        if ( row["_car"].asUInt64() == static_cast<uint64_t>( carId ) ) {
            rows.push_back( row );
        }
    }
    return rows;
}

// vehicules/colors.bnk colors.2dm (GServiceCarColors), loaded once.
const Render2DM *SharedColorLibrary() {
    static Render2DM library;
    static bool bTried = false;
    static bool bLoaded = false;
    if ( !bTried ) {
        bTried = true;
        Bank bank;
        if ( bank.load( ( gpFile->getBankFolder() + "vehicules/colors.bnk" ).c_str() ) ) {
            for ( const BankEntry &entry : bank.getEntries() ) {
                if ( entry.Extension == "2dm" && library.parse( entry.pData, entry.Size ) ) {
                    bLoaded = true;
                    break;
                }
            }
        }
    }
    return bLoaded ? &library : nullptr;
}

const MaterialDesc *FindNamedMaterial( const Render2DM *pLibrary, const std::string &name ) {
    if ( pLibrary == nullptr || name.empty() || name == "##" ) {
        return nullptr;
    }
    // FUN_00c4c0f0( hash( name ) ) [CODE]: by hashcode — the MAT. name field
    // is cut to 15 characters ("Nuovo_Grigio_In"), so a long colour id
    // ("Spider_16M_Ext_Rouge_01") only matches by its hash.
    if ( const MaterialDesc *pMaterial = pLibrary->find( GetIdentifier64bit( name.c_str() ) ) ) {
        return pMaterial;
    }
    for ( const MaterialDesc &material : pLibrary->getOrderedMaterials() ) {
        if ( StringCmpI( material.Name.c_str(), name.c_str() ) == 0 ) {
            return &material;
        }
    }
    return nullptr;
}

// TD_CarPaint2 shader set GUID (paint_spec.md 4) [DATA].
constexpr uint64_t kTDCarPaint2 = 0x183157b81c6c5377ull;

// The paint parameters FUN_00c5ff20 mode 0 copies (paint_spec.md 3.3) [CODE].
const char *const kPaintParameters[] = { "MATPARAM", "SPECPARM", "SPECBLINN", "FLAKEABNS", "RAMPPOW", "RAMPCLR1",
                                         "RAMPCLR2", "RAMPCLR3", "RAMPFLAKE", "BLINNCLRA", "BLINNCLRB" };
} // namespace

int32_t CinematicCar::CountDatabasePaints( int32_t carId ) { return static_cast<int32_t>( PaintRows( carId ).size() ); }

std::vector<CinematicCar::PaintInfo> CinematicCar::DatabasePaints( int32_t carId ) {
    std::vector<PaintInfo> paints;
    for ( const XmbValue &row : PaintRows( carId ) ) {
        PaintInfo info;
        info.Name = row["_color_name"].asString();
        info.Price = static_cast<int32_t>( row["_price"].asInt() );
        paints.push_back( info );
    }
    return paints;
}

std::string CinematicCar::DatabasePaintColorId( int32_t carId, int32_t paintIndex ) {
    const std::vector<XmbValue> rows = PaintRows( carId );
    if ( paintIndex < 0 || paintIndex >= static_cast<int32_t>( rows.size() ) ) {
        return "";
    }
    return rows[static_cast<size_t>( paintIndex )]["_color_id_1"].asString();
}

namespace {
bool SwatchOf( const MaterialDesc &material, float outRgb[3] ) {
    Vector4f value;
    if ( material.getParameter( "RAMPCLR1", value ) ) {
        outRgb[0] = value.x;
        outRgb[1] = value.y;
        outRgb[2] = value.z;
    } else {
        outRgb[0] = material.DiffuseColor.x;
        outRgb[1] = material.DiffuseColor.y;
        outRgb[2] = material.DiffuseColor.z;
    }
    for ( int32_t i = 0; i < 3; i++ ) {
        outRgb[i] = outRgb[i] < 0.0f ? 0.0f : ( outRgb[i] > 1.0f ? 1.0f : outRgb[i] );
    }
    return true;
}
} // namespace

bool CinematicCar::FindLibrarySwatch( const std::string &materialName, float outRgb[3] ) {
    const MaterialDesc *pMaterial = FindNamedMaterial( SharedColorLibrary(), materialName );
    return pMaterial != nullptr && SwatchOf( *pMaterial, outRgb );
}

bool CinematicCar::getDatabasePaintSwatch( int32_t paintIndex, float outRgb[3] ) {
    const std::vector<XmbValue> rows = PaintRows( getIdCar() );
    if ( paintIndex < 0 || paintIndex >= static_cast<int32_t>( rows.size() ) ) {
        return false;
    }
    const std::string colorId = rows[paintIndex]["_color_id_1"].asString();
    Render2DM carLibrary;
    const BankEntry *pCarColors = carBank.findEntry( ( ToLower( fileName ) + "_color" ).c_str(), "2dm" );
    const bool bCarLibrary = pCarColors != nullptr && carLibrary.parse( pCarColors->pData, pCarColors->Size );
    const MaterialDesc *pMaterial = FindNamedMaterial( bCarLibrary ? &carLibrary : nullptr, colorId );
    if ( pMaterial == nullptr ) {
        pMaterial = FindNamedMaterial( SharedColorLibrary(), colorId );
    }
    return pMaterial != nullptr && SwatchOf( *pMaterial, outRgb );
}

std::string CinematicCar::applyDatabasePaint( int32_t paintIndex ) {
    static const bool bLog = CmdLine::HasArg( "-logpaint" );
    const std::vector<XmbValue> rows = PaintRows( getIdCar() );
    if ( rows.empty() ) {
        return "";
    }
    if ( paintIndex < 0 || paintIndex >= static_cast<int32_t>( rows.size() ) ) {
        paintIndex = 0;
    }
    const XmbValue &row = rows[paintIndex];
    const std::string colorId = row["_color_id_1"].asString();
    const std::string colorId2 = row["_color_id_2"].asString();
    const std::string colorName = row["_color_name"].asString();
    // The car's own colour library (a second .2dm of the car bank, FUN_00c4cc30
    // registration) first, then colors.2dm.
    Render2DM carLibrary;
    const BankEntry *pCarColors = carBank.findEntry( ( ToLower( fileName ) + "_color" ).c_str(), "2dm" );
    const bool bCarLibrary = pCarColors != nullptr && carLibrary.parse( pCarColors->pData, pCarColors->Size );
    const auto resolve = [&]( const std::string &name ) {
        const MaterialDesc *pMaterial = FindNamedMaterial( bCarLibrary ? &carLibrary : nullptr, name );
        return pMaterial != nullptr ? pMaterial : FindNamedMaterial( SharedColorLibrary(), name );
    };
    const MaterialDesc *pColor = resolve( colorId );
    const MaterialDesc *pColorN = resolve( colorId + "_N" );
    const MaterialDesc *pColor2 = resolve( colorId2 );
    if ( bLog ) {
        OTDU_LOG_INFO( "CarPaint: %s: %zu row(s), row %d '%s' color '%s' (%s%s) / '%s' (%s) callipers '%s'\n",
                       fileName.c_str(), rows.size(), paintIndex, colorName.c_str(), colorId.c_str(),
                       pColor != nullptr ? "found" : "no material", pColorN != nullptr ? " + _N" : "", colorId2.c_str(),
                       pColor2 != nullptr ? "found" : "none", row["_callipers"].asString().c_str() );
        if ( bCarLibrary ) {
            std::string names;
            for ( const MaterialDesc &material : carLibrary.getOrderedMaterials() ) {
                names += " " + material.Name;
            }
            OTDU_LOG_INFO( "CarPaint:   %s_color.2dm:%s\n", fileName.c_str(), names.c_str() );
        }
        for ( const EdenMaterial &material : car.getMaterials() ) {
            if ( material.ShaderSetGUID == kTDCarPaint2 || material.Name.rfind( "Paint", 0 ) == 0 ) {
                OTDU_LOG_INFO( "CarPaint:   car material '%s' shader 0x%llx\n", material.Name.c_str(),
                               static_cast<unsigned long long>( material.ShaderSetGUID ) );
            }
        }
    }
    if ( pColor == nullptr ) {
        return "";
    }
    // The paint slots, by material name [INF: the targets of the ENC paint
    // state; the names are the cars' own, -logpaint]: colour 1 (mode 0) onto
    // the body Paint, its skinned part Paint_SK (the California's roof) and
    // the low resolution Paint_LR / Paint_MR; colour 2 (mode 10, bicolour)
    // onto Paint2 / Paint2_SK. The other TD_CarPaint2 materials are liveries
    // and trims (Carbon, Bande_Noir, the 16M's blanc / rouge / Vert) and keep
    // their own paint. <id>_N goes onto a "_N" material when the library has it.
    const auto copyPaint = [&]( EdenMaterial &material, const MaterialDesc &source ) {
        for ( const char *pName : kPaintParameters ) {
            Vector4f value;
            if ( source.getParameter( pName, value ) ) {
                material.Parameters[GetIdentifier64bit( pName )] = value;
            }
        }
        material.Diffuse = source.DiffuseColor;
        material.Ambient = source.AmbientColor;
        material.Specular = source.SpecularColor;
    };
    const auto isOneOf = [&]( const std::string &name, std::initializer_list<const char *> names ) {
        for ( const char *pName : names ) {
            if ( StringCmpI( name.c_str(), pName ) == 0 ) {
                return true;
            }
        }
        return false;
    };
    size_t painted = 0;
    for ( EdenMaterial &material : car.getMaterials() ) {
        if ( isOneOf( material.Name, { "Paint", "Paint_SK", "Paint_LR", "Paint_MR", "Paint_N" } ) ) {
            const bool bNVariant = material.Name.size() > 2 && material.Name.compare( material.Name.size() - 2, 2, "_N" ) == 0;
            copyPaint( material, ( bNVariant && pColorN != nullptr ) ? *pColorN : *pColor );
            painted++;
        } else if ( pColor2 != nullptr && isOneOf( material.Name, { "Paint2", "Paint2_SK" } ) ) {
            copyPaint( material, *pColor2 );
            painted++;
        }
    }
    if ( bLog ) {
        OTDU_LOG_INFO( "CarPaint: %s painted '%s' (%s%s%s) on %zu material(s)\n", fileName.c_str(), colorName.c_str(),
                       colorId.c_str(), pColor2 != nullptr ? " + " : "", pColor2 != nullptr ? colorId2.c_str() : "",
                       painted );
    }
    // The clean paint values just moved: applySurfaceState must recapture its
    // baseline or the next frame would blend the dirt over the NEW clean value
    // and the tint would never come back to this paint.
    paintBaselineValid = false;
    return colorName;
}

bool CinematicCar::applyColor( const std::string &colorId ) {
    // A colour by material id (FUN_00a50ca0 -> FUN_00d8e6a0 [CODE]): the
    // DBCarsAndColors row of the car whose _color_id_1 is that id, painted by
    // the same machinery as applyDatabasePaint (Paint / Paint_SK / _N /
    // bicolour Paint2). False when the id is no row of this car ("" / "##":
    // no change, as upstream).
    if ( colorId.empty() || colorId == "##" || !car.isLoaded() ) {
        return false;
    }
    const std::vector<XmbValue> rows = PaintRows( getIdCar() );
    for ( size_t i = 0; i < rows.size(); i++ ) {
        if ( rows[i]["_color_id_1"].asString() == colorId ) {
            return !applyDatabasePaint( static_cast<int32_t>( i ) ).empty();
        }
    }
    OTDU_LOG_WARN( "CinematicCar: %s: colour '%s' is not a DBCarsAndColors row\n", fileName.c_str(), colorId.c_str() );
    return false;
}

// The Desc of a database car for a given rims row (the shop slots carry
// _default_rims; 0 = the car's default rims, as DescFromDatabase) [CODE,
// FUN_009f4080 slot car]. True even when the rims row is missing (the .2dm
// rims are kept).
bool CinematicCar::FindDatabaseDesc( int32_t idCar, uint64_t rimsHashcode, Desc &outDesc ) {
    if ( gpDatabase == nullptr ) {
        return false;
    }
    XmbValue root = gpDatabase->getDocument().getRoot();
    bool bFound = false;
    for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ) && !bFound; i++ ) {
        XmbValue car = root.getArrayElement( "DBCarPhysicsData", i );
        if ( car["_id_car"].asInt( -2 ) == idCar ) {
            outDesc.FileName = car["_car_file_name"].asString();
            if ( rimsHashcode == 0 ) {
                rimsHashcode = car["_default_rims"].asUInt64();
            }
            bFound = true;
        }
    }
    if ( !bFound ) {
        return false;
    }
    for ( uint32_t i = 0; i < root.getArraySize( "DBRims" ); i++ ) {
        XmbValue rim = root.getArrayElement( "DBRims", i );
        if ( rim["_hashcode"].asUInt64() != rimsHashcode ) {
            continue;
        }
        const std::string path = ToLower( rim["_rsc_path"].asString() );
        outDesc.RimFront = path + "/" + ToLower( rim["_rsc_file_name_front"].asString() );
        outDesc.RimRear = path + "/" + ToLower( rim["_rsc_file_name_rear"].asString() );
        outDesc.DiameterFront = static_cast<int32_t>( rim["_diameter_front"].asInt( 18 ) );
        outDesc.DiameterRear = static_cast<int32_t>( rim["_diameter_rear"].asInt( 18 ) );
        outDesc.TireRadius = outDesc.DiameterFront * 0.0254f * 0.5f +
                             rim["_largeur_front"].asFloat( 245.0f ) * rim["_hauteur_front"].asFloat( 40.0f ) * 1e-5f;
        return true;
    }
    return true;
}

bool CinematicCar::DescFromDatabase( int32_t carId, Desc &outDesc ) {
    if ( gpDatabase == nullptr ) {
        return false;
    }
    XmbValue root = gpDatabase->getDocument().getRoot();
    uint64_t rimHashcode = 0;
    bool bFound = false;
    for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
        XmbValue record = root.getArrayElement( "DBCarPhysicsData", i );
        if ( record["_id_car"].asInt( -2 ) == carId ) {
            outDesc.FileName = record["_car_file_name"].asString();
            rimHashcode = record["_default_rims"].asUInt64();
            bFound = !outDesc.FileName.empty();
            break;
        }
    }
    if ( !bFound ) {
        return false;
    }
    for ( uint32_t i = 0; i < root.getArraySize( "DBRims" ); i++ ) {
        XmbValue rimDesc = root.getArrayElement( "DBRims", i );
        if ( rimDesc["_hashcode"].asUInt64() == rimHashcode ) {
            const std::string path = ToLower( rimDesc["_rsc_path"].asString() );
            outDesc.RimFront = path + "/" + ToLower( rimDesc["_rsc_file_name_front"].asString() );
            outDesc.RimRear = path + "/" + ToLower( rimDesc["_rsc_file_name_rear"].asString() );
            outDesc.DiameterFront = static_cast<int32_t>( rimDesc["_diameter_front"].asInt( 18 ) );
            // Tire: rim radius + sidewall (width x aspect ratio, "245/40").
            outDesc.TireRadius = outDesc.DiameterFront * 0.0254f * 0.5f +
                                 rimDesc["_largeur_front"].asFloat( 245.0f ) * rimDesc["_hauteur_front"].asFloat( 40.0f ) * 1e-5f;
            outDesc.DiameterRear = static_cast<int32_t>( rimDesc["_diameter_rear"].asInt( 18 ) );
            break;
        }
    }
    return true;
}

bool CinematicCar::getNodeModelTransform( const char *pName, Matrix4x4f &outTransform ) const {
    if ( !car.isLoaded() ) {
        return false;
    }
    int32_t node = car.getScene().findNode( pName );
    if ( node < 0 ) {
        node = FindNodeByHash( car.getScene(), pName );
    }
    if ( node < 0 ) {
        return false;
    }
    outTransform = car.getNodeTransform( node );
    return true;
}

bool CinematicCar::getBodyBounds( Vector3f &outMin, Vector3f &outMax ) const {
    if ( !car.isLoaded() ) {
        return false;
    }
    const int32_t body = car.getScene().findNode( "BODY_HR" );
    return body >= 0 && car.getNodeBounds( body, outMin, outMax );
}

Matrix4x4f CinematicCar::fitOnGround( Vector3f position, const Quaternionf &rotation,
                                      const SurfaceFunction &surfaceHeight ) const {
    const Matrix4x4f cinematicRotation = EdenRotationFromQuaternion( rotation );
    Matrix4x4f tilt = Matrix4x4f::Identity();
    // The cinematic gives the car pose above the road (heading only): in
    // game the physics drops it on its wheels. Rest the 4 tires on the road
    // surface (sector road meshes or heightfield): plane through the hub
    // targets (surface + tire radius), car up = plane normal.
    // Bikes have two wheels in line: the plane fit below needs three points, so
    // they were never placed on the road and the ray suspension started above
    // it (no wheel contact, the bike sat on its chassis). A two-wheeler rests
    // on the average of its two tire bottoms instead [INF].
    if ( wheelNodes.size() == 2 && !CmdLine::HasArg( "-nocarground" ) ) {
        const Matrix4x4f pose = Matrix4x4f::Translation( position ) * cinematicRotation;
        float lift = 0.0f;
        int32_t found = 0;
        for ( int32_t node : wheelNodes ) {
            const Vector3f hubLocal = car.getNodeTransform( node ).getTranslation();
            const Vector3f hub = pose.transformPoint( hubLocal );
            float surface = 0.0f;
            if ( surfaceHeight( hub.x, hub.z, hub.y + 1.5f, surface ) ) {
                lift += surface + tireRadius - hub.y;
                found++;
            }
        }
        if ( found > 0 ) {
            position.y += lift / static_cast<float>( found );
        }
    }
    if ( wheelNodes.size() >= 3 && !CmdLine::HasArg( "-nocarground" ) ) {
        const Matrix4x4f pose = Matrix4x4f::Translation( position ) * cinematicRotation;
        std::vector<Vector3f> targets;
        float hubHeight = 0.0f;
        for ( int32_t node : wheelNodes ) {
            const Vector3f hubLocal = car.getNodeTransform( node ).getTranslation();
            const Vector3f hub = pose.transformPoint( hubLocal );
            float surface = 0.0f;
            if ( surfaceHeight( hub.x, hub.z, hub.y + 1.5f, surface ) ) {
                targets.push_back( Vector3f( hub.x - position.x, surface + tireRadius, hub.z - position.z ) );
                hubHeight += hubLocal.y;
            }
        }
        if ( targets.size() >= 3 ) {
            hubHeight /= static_cast<float>( targets.size() );
            // Least squares y = a x + b z + c.
            double sxx = 0, sxz = 0, szz = 0, sx = 0, sz = 0, sy = 0, sxy = 0, szy = 0;
            for ( const Vector3f &t : targets ) {
                sxx += t.x * t.x;
                sxz += t.x * t.z;
                szz += t.z * t.z;
                sx += t.x;
                sz += t.z;
                sy += t.y;
                sxy += t.x * t.y;
                szy += t.z * t.y;
            }
            const double n = static_cast<double>( targets.size() );
            // Normal equations [sxx sxz sx; sxz szz sz; sx sz n] (a b c) = (sxy szy sy)
            const double m[3][3] = { { sxx, sxz, sx }, { sxz, szz, sz }, { sx, sz, n } };
            const double r[3] = { sxy, szy, sy };
            const double det = m[0][0] * ( m[1][1] * m[2][2] - m[1][2] * m[2][1] ) -
                               m[0][1] * ( m[1][0] * m[2][2] - m[1][2] * m[2][0] ) +
                               m[0][2] * ( m[1][0] * m[2][1] - m[1][1] * m[2][0] );
            if ( std::abs( det ) > 1e-9 ) {
                auto solve = [&]( int column ) {
                    double c[3][3];
                    for ( int i = 0; i < 3; i++ ) {
                        for ( int j = 0; j < 3; j++ ) {
                            c[i][j] = j == column ? r[i] : m[i][j];
                        }
                    }
                    return ( c[0][0] * ( c[1][1] * c[2][2] - c[1][2] * c[2][1] ) -
                             c[0][1] * ( c[1][0] * c[2][2] - c[1][2] * c[2][0] ) +
                             c[0][2] * ( c[1][0] * c[2][1] - c[1][1] * c[2][0] ) ) /
                           det;
                };
                const float slopeX = static_cast<float>( solve( 0 ) );
                const float slopeZ = static_cast<float>( solve( 1 ) );
                const float height = static_cast<float>( solve( 2 ) );
                const Vector3f normal = Vector3f( -slopeX, 1.0f, -slopeZ ).normalized();
                const Vector3f axis = Vector3f( 0.0f, 1.0f, 0.0f ).cross( normal );
                const float sine = axis.length();
                if ( sine > 1e-6f ) {
                    tilt = Matrix4x4f::FromQuaternion(
                        Quaternionf::FromAxisAngle( axis * ( 1.0f / sine ), std::atan2( sine, normal.y ) ) );
                }
                // Hub plane at the car centre, car origin hubHeight below it.
                position.y = height;
                position = position - normal * hubHeight;
            }
        }
    }
    if ( CmdLine::HasArg( "-logcar" ) ) {
        const Matrix4x4f world = Matrix4x4f::Translation( position ) * tilt * cinematicRotation;
        for ( int32_t node : wheelNodes ) {
            const Vector3f hub = world.transformPoint( car.getNodeTransform( node ).getTranslation() );
            float surface = -1000.0f;
            surfaceHeight( hub.x, hub.z, hub.y + 1.5f, surface );
            OTDU_LOG_INFO( "wheel %d hub (%.3f %.3f %.3f) bottom %.3f surface %.3f\n", node, hub.x, hub.y, hub.z,
                           hub.y - tireRadius, surface );
        }
    }
    static const bool bLogCar = CmdLine::HasArg( "-logcar" );
    if ( bLogCar ) {
        OTDU_LOG_INFO( "Car pos (%.3f %.3f %.3f) q (%.4f %.4f %.4f %.4f)\n", position.x, position.y, position.z,
                       rotation.x, rotation.y, rotation.z, rotation.w );
    }
    return Matrix4x4f::Translation( position ) * tilt * cinematicRotation;
}

// The retail inside view swaps the car "Glass" material for the interior
// bank's Glass_I (TD_CarGlassDirt, california_spec.md 4: FUN_00c53d10 on the
// camera flag 0x40000): the outside glass (ps_109 + the DIRTCOLOR dirt) is a
// near-opaque wall from the driver seat otherwise (docs/STATE.md session 45).
// The wiper driver (setWiperMode's comment): the retail car models bake
// their parked blades into the body as one wide thin strip prim at the glass
// base (the Delta: a 1.88 x 0.11 x 0.05 m 'Mirrors' prim inside BODY_MR; the
// washer jets are the SPRG nodes with WS_SPRG) - no pivot node, no animation,
// no exe driver anywhere in the readable data or the PS3 build. collectWipers
// finds that prim (a wide thin strip high on the body, in front of the
// driver), splits its triangles into the two blades at x = 0 and uploads each
// half as its own index list over the item's vertex streams; updateWipers
// swings the halves about pivots at their outer ends (axis = the windscreen
// normal [INF: the Delta's 34 degree rake, all cars]) and draw() re-draws
// them while the parked prim is hidden - at rest angle 0 the halves reproduce
// the parked art exactly.
void CinematicCar::collectWipers() {
    if ( !car.isLoaded() ) {
        return;
    }
    Vector3f bodyMin, bodyMax;
    const bool bHaveBody = getBodyBounds( bodyMin, bodyMax );
    const std::vector<EdenModel::Item> &items = car.getItems();
    int32_t best = -1;
    float bestWidth = 0.0f;
    for ( size_t i = 0; i < items.size(); i++ ) {
        const EdenModel::Item &item = items[i];
        if ( item.PrimitiveIndices.empty() || item.PrimitiveVertices.empty() ) {
            continue;
        }
        const Vector3f size = item.BoundsMax - item.BoundsMin;
        // One strip across the glass base: wide, flat, shallow.
        if ( size.x < 1.2f || size.y > 0.35f || size.z > 0.30f ) {
            continue;
        }
        const Vector3f center = ( item.BoundsMin + item.BoundsMax ) * 0.5f;
        // Up on the body, ahead of the driver (the windscreen band).
        if ( bHaveBody ) {
            if ( center.y < bodyMin.y + ( bodyMax.y - bodyMin.y ) * 0.40f || center.z < 0.0f ) {
                continue;
            }
        }
        if ( size.x > bestWidth ) {
            bestWidth = size.x;
            best = static_cast<int32_t>( i );
        }
    }
    if ( best < 0 ) {
        car.clearPrimitiveData();
        return; // this car bakes no wiper strip (or it did not match)
    }
    const EdenModel::Item &item = items[static_cast<size_t>( best )];
    const Vector3f size = item.BoundsMax - item.BoundsMin;
    const Vector3f center = ( item.BoundsMin + item.BoundsMax ) * 0.5f;
    // Split the triangles into the two blades at x = 0 (the parked art is one
    // strip: the halves meet in the middle of the glass).
    std::vector<uint32_t> halves[2];
    Vector3f halfMin[2] = { Vector3f( 1e9f, 1e9f, 1e9f ), Vector3f( 1e9f, 1e9f, 1e9f ) };
    Vector3f halfMax[2] = { Vector3f( -1e9f, -1e9f, -1e9f ), Vector3f( -1e9f, -1e9f, -1e9f ) };
    const std::vector<uint32_t> &indices = item.PrimitiveIndices;
    const std::vector<Vector3f> &vertices = item.PrimitiveVertices;
    for ( size_t t = 0; t + 2 < indices.size(); t += 3 ) {
        const Vector3f &a = vertices[indices[t]];
        const Vector3f &b = vertices[indices[t + 1]];
        const Vector3f &c = vertices[indices[t + 2]];
        const int32_t half = ( a.x + b.x + c.x ) < 0.0f ? 0 : 1;
        for ( uint32_t k = 0; k < 3; k++ ) {
            halves[half].push_back( indices[t + k] );
        }
        halfMin[half] = Vector3f( Min( halfMin[half].x, Min( a.x, Min( b.x, c.x ) ) ),
                                  Min( halfMin[half].y, Min( a.y, Min( b.y, c.y ) ) ),
                                  Min( halfMin[half].z, Min( a.z, Min( b.z, c.z ) ) ) );
        halfMax[half] = Vector3f( Max( halfMax[half].x, Max( a.x, Max( b.x, c.x ) ) ),
                                  Max( halfMax[half].y, Max( a.y, Max( b.y, c.y ) ) ),
                                  Max( halfMax[half].z, Max( a.z, Max( b.z, c.z ) ) ) );
    }
    wiperNode = items[static_cast<size_t>( best )].NodeIndex;
    RenderDevice &device = gpRender->getDevice();
    for ( uint32_t h = 0; h < 2; h++ ) {
        if ( halves[h].empty() ) {
            continue;
        }
        WiperHalf wiper;
        wiper.ItemIndex = best;
        wiper.Mesh = item.Mesh; // the item's vertex streams and layout
        wiper.pIndices =
            device.createBuffer( halves[h].size() * sizeof( uint32_t ), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                 halves[h].data() );
        wiper.Mesh.pIndexBuffer = wiper.pIndices;
        wiper.Mesh.FirstIndex = 0;
        wiper.Mesh.IndexCount = static_cast<uint32_t>( halves[h].size() );
        wiper.IndexCount = wiper.Mesh.IndexCount;
        // Pivot at the outer end (a blade width in), the blade reaching to
        // the half's inner end (the parked pose of the art).
        const float inset = Min( 0.20f, size.x * 0.15f );
        if ( h == 0 ) {
            wiper.Pivot = Vector3f( halfMin[0].x + inset, center.y, center.z );
            wiper.Blade = Vector3f( halfMax[0].x - wiper.Pivot.x, 0.0f, 0.0f );
        } else {
            wiper.Pivot = Vector3f( halfMax[1].x - inset, center.y, center.z );
            wiper.Blade = Vector3f( halfMin[1].x - wiper.Pivot.x, 0.0f, 0.0f );
        }
        wipers.push_back( std::move( wiper ) );
    }
    car.clearPrimitiveData();
    OTDU_LOG_INFO( "CinematicCar: wipers %s (item %d '%s' %.2f x %.2f x %.2f, halves %u + %u indices)\n",
                   wipers.size() == 2 ? "found" : "partial", best, car.getScene().getNodes()[wiperNode].Name.c_str(),
                   size.x, size.y, size.z,
                   wipers.size() > 0 ? wipers[0].IndexCount : 0, wipers.size() > 1 ? wipers[1].IndexCount : 0 );
}

void CinematicCar::setWiperMode( eWiperMode mode ) {
    wiperMode = mode;
    if ( mode == eWiperMode::Off ) {
        wiperTimer = 0.0f;
    }
}

void CinematicCar::updateWipers( float deltaTime, float rainAlpha ) {
    if ( wipers.empty() || deltaTime <= 0.0f ) {
        return;
    }
    // Sweep constants [INF]: a full sweep takes 1.1 s up to 89 degrees; the
    // auto interval follows the rain (7 s drizzle to 1.2 s downpour).
    constexpr float kSweepTime = 1.1f;
    constexpr float kMaxAngle = 1.55f;
    constexpr float kIntervalMin = 1.2f;
    constexpr float kIntervalMax = 7.0f;
    auto park = [this, deltaTime]() {
        wiperAngle = Max( 0.0f, wiperAngle - 3.0f * deltaTime );
        if ( wiperAngle <= 0.0f ) {
            wiperTimer = 0.0f;
        }
    };
    const bool bActive = wiperMode == eWiperMode::Fast || ( wiperMode == eWiperMode::Auto && rainAlpha > 0.001f );
    if ( !bActive ) {
        park();
        return;
    }
    if ( wiperTimer < 0.0f ) {
        // Sweeping: -timer is the sweep progress 0..1.
        wiperTimer += deltaTime / kSweepTime;
        if ( wiperTimer >= 0.0f ) {
            wiperAngle = 0.0f;
            wiperTimer = wiperMode == eWiperMode::Fast
                             ? 0.0f
                             : Max( kIntervalMin,
                                    kIntervalMax - Clamp( rainAlpha / 255.0f, 0.0f, 1.0f ) * ( kIntervalMax - kIntervalMin ) );
        } else {
            wiperAngle = kMaxAngle * sinf( 3.14159265f * -wiperTimer );
        }
        return;
    }
    // Waiting for the next sweep (0: sweep right away).
    wiperTimer -= deltaTime;
    if ( wiperTimer <= 0.0f ) {
        wiperTimer = -1e-6f;
    }
}

bool CinematicCar::getWiperSweep( uint32_t half, Vector3f &outPivot, Vector3f &outTip ) const {
    if ( wipers.empty() || wiperAngle <= 0.001f || half >= wipers.size() ) {
        return false;
    }
    const WiperHalf &wiper = wipers[half];
    const Matrix4x4f nodeT = wiperNode >= 0 ? car.getNodeTransform( wiperNode ) : Matrix4x4f();
    // The swing of draw(): the blade rotated about the windscreen normal
    // through the pivot (mirrored on the right half).
    const Vector3f axis( 0.0f, 0.557f, 0.830f ); // the Delta's 34 degree rake [INF]
    const float angle = ( half == 0 ? 1.0f : -1.0f ) * wiperAngle;
    const Matrix4x4f swing = Matrix4x4f::Translation( wiper.Pivot ) *
                             Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( axis, angle ) ) *
                             Matrix4x4f::Translation( Vector3f( -wiper.Pivot.x, -wiper.Pivot.y, -wiper.Pivot.z ) );
    outPivot = nodeT.transformPoint( wiper.Pivot );
    outTip = nodeT.transformPoint( swing.transformPoint( wiper.Pivot + wiper.Blade ) );
    return true;
}

void CinematicCar::applyGlassSwap( bool bInside ) {
    if ( !bGlassGathered ) {
        bGlassGathered = true;
        if ( !interior.isLoaded() ) {
            return;
        }
        int32_t glassI = -1;
        for ( uint32_t i = 0; i < interior.getMaterials().size(); i++ ) {
            if ( StringCmpI( interior.getMaterials()[i].Name.c_str(), "Glass_I" ) == 0 ) {
                glassI = static_cast<int32_t>( i );
                break;
            }
        }
        if ( glassI < 0 ) {
            return; // this interior carries no Glass_I (nothing to swap to)
        }
        glassDirt = interior.getMaterials()[glassI];
        for ( uint32_t i = 0; i < car.getMaterials().size(); i++ ) {
            if ( StringCmpI( car.getMaterials()[i].Name.c_str(), "Glass" ) == 0 ) {
                glassSwap.emplace_back( &car, static_cast<int32_t>( i ) );
            }
        }
        glassOriginals.reserve( glassSwap.size() );
        for ( const auto &swap : glassSwap ) {
            glassOriginals.push_back( swap.first->getMaterials()[static_cast<size_t>( swap.second )] );
        }
        OTDU_LOG_DEBUG( "CinematicCar: glass swap %zu material(s) -> Glass_I\n", glassSwap.size() );
    }
    if ( glassSwap.empty() || bGlassSwapped == bInside ) {
        return;
    }
    for ( size_t k = 0; k < glassSwap.size(); k++ ) {
        EdenMaterial &material = glassSwap[k].first->getMaterials()[static_cast<size_t>( glassSwap[k].second )];
        material = bInside ? glassDirt : glassOriginals[k];
    }
    bGlassSwapped = bInside;
}

void CinematicCar::draw( EdenRenderer &renderer, const Matrix4x4f &world, const WeatherPreset *pWeather,
                         bool bEnvironmentAssigned ) {
    if ( !car.isLoaded() ) {
        return;
    }
    // Car lighting factors of the weather (EnvCoeff, in the car shaders:
    // x: reflection (x MatParam.x), y: specular, z / w: fresnel coefficient /
    // power (x MatParam.zw)). The static cubemap of common_car.bnk stands in
    // for the dynamic one.
    const Vector4f fresnel = pWeather != nullptr ? pWeather->getVector4( "car_fresnel_coeff_power_boost_specular",
                                                                         Vector4f( 1.0f, 1.0f, 1.0f, 1.0f ) )
                                                 : Vector4f( 1.0f, 1.0f, 1.0f, 1.0f );
    auto factor = [pWeather]( const char *pName, float defaultValue ) {
        return pWeather != nullptr ? pWeather->getFloat( pName, defaultValue ) : defaultValue;
    };
    const float specular = fresnel.w * factor( "car_sun_specular_factor", 1.0f );
    const float staticFactor = bEnvironmentAssigned ? 1.0f : factor( "car_envmap_factor_static_cubemap", 0.7f );
    const Vector4f envCoeff( factor( "car_envmap_factor", 1.0f ) * staticFactor, specular, fresnel.x, fresnel.y );
    const Vector4f rimEnvCoeff( factor( "car_rim_envmap_factor", 1.0f ) * staticFactor, specular, fresnel.x,
                                fresnel.y );
    // The dynamic surface state (applySurfaceState): a dirty body loses its
    // reflection, a wet one gains a little (the water film acts as a mirror on
    // the clean paint). The body carries stainVisual / wetVisual; the rims and
    // the tires keep the weather values.
    const float gloss = ( 1.0f - 0.85f * stainVisual ) * ( 1.0f + 0.45f * wetVisual );
    Vector4f bodyEnvCoeff = envCoeff;
    bodyEnvCoeff.x = envCoeff.x * gloss;
    static const uint64_t kEnvCoeff = GetIdentifier64bit( "ENVCOEFF" );
    for ( EdenModel *pModel : { &car, &rim, &rimRear, &tire } ) {
        for ( EdenMaterial &material : pModel->getMaterials() ) {
            auto it = material.Parameters.find( kEnvCoeff );
            if ( it != material.Parameters.end() ) {
                it->second = ( pModel == &rim || pModel == &rimRear ) ? rimEnvCoeff
                                                                     : ( pModel == &car ? bodyEnvCoeff : envCoeff );
            }
        }
    }

    for ( size_t i = 0; i < wheelShadowNodes.size() && i < wheelNodes.size(); i++ ) {
        const Vector3f hub = car.getNodeTransform( wheelNodes[i] ).getTranslation();
        const Matrix4x4f box = Matrix4x4f::Translation( Vector3f( hub.x, hub.y - tireRadius, hub.z ) ) *
                               Matrix4x4f::Scale( Vector3f( 0.25f, 0.12f, 0.45f ) );
        car.setNodeTransform( wheelShadowNodes[i], box );
    }
    // Car level of detail (FUN_00c4e630 / FUN_00c4dcb0): distance from the
    // camera x camera factor (+0x17c, taken as 1 [INF]) against the table
    // 0x00f7e788 {250, 125, 15, 5, 0}: below 5 m LOD 4 (interior shown).
    // LODs 0..2 are not used here (high resolution kept beyond 15 m).
    // The level is chosen once per frame for the view camera: the shadow
    // caster and reflection passes keep the last one.
    bool bInterior = bLastInterior;
    if ( !renderer.isShadowCasterMode() && !renderer.isReflectionPass() ) {
        const float distance = ( world.getTranslation() - renderer.getSceneParams().CameraPosition ).length();
        static const int32_t forcedLod = CmdLine::GetArgInt( "-carlod", -1 );
        bInterior = interior.isLoaded() && !interiorDrawnNodes.empty() &&
                    ( forcedLod >= 0 ? forcedLod == 4 : distance < 5.0f );
        static const bool bLogLod = CmdLine::HasArg( "-logcar" );
        if ( bLogLod && bInterior != bLastInterior ) {
            OTDU_LOG_INFO( "CinematicCar: camera %.2f m, LOD %d\n", distance, bInterior ? 4 : 3 );
        }
        bLastInterior = bInterior;
    }
    for ( int32_t node : cockpitLowNodes ) {
        car.setNodeVisible( node, !bInterior );
    }
    // Inside views: the exterior glass (dirty windscreen art + its reflection)
    // is what makes the view hard to see out of; hide it there only.
    {
        static const bool bKeepGlass = CmdLine::HasArg( "-keepglass" );
        for ( int32_t node : glassNodes ) {
            car.setNodeVisible( node, !bInterior || bKeepGlass );
        }
    }
    for ( int32_t node : alwaysHiddenNodes[0] ) {
        car.setNodeVisible( node, false );
    }
    for ( int32_t node : alwaysHiddenNodes[1] ) {
        interior.setNodeVisible( node, false );
    }
    applyGlassSwap( bInterior && !CmdLine::HasArg( "-keepglass" ) ); // -keepglass: the A/B
    // The light state (FUN_00c588d0): the GLOSSFCT channels, the lamp glow
    // node flags and the light box INTENSITY — before the draw, for this
    // frame's view camera (the shadow / reflection passes reuse the state).
    applyLights( pWeather );
    // The swinging wipers: the parked strip prim hides (the halves re-draw
    // its triangles at the sweep angle below). The hardtop skinned path has
    // no draw params - a roof car's wipers stay parked there [INF: the
    // convertibles this path serves show no windscreen blades anyway].
    const bool bWipersSwinging = !wipers.empty() && wiperAngle > 0.001f && !bHardtop;
    EdenDrawParams wiperParams;
    wiperParams.ForceHiddenItems = lowResItems;
    // Without it a params draw records its blended items at once, out of the
    // sorted blended pass: the head lamp glass then darkened the lamps (seen
    // on the Range Rover / Mustang A/B, mean diff 0.33).
    wiperParams.bDeferBlended = true;
    if ( bWipersSwinging ) {
        for ( const WiperHalf &wiper : wipers ) {
            wiperParams.ForceHiddenItems.push_back( wiper.ItemIndex );
        }
    }
    if ( bHardtop ) {
        car.drawSkinned( renderer, world, hardtopSkin );
    } else {
        car.draw( renderer, world, !wiperParams.ForceHiddenItems.empty() ? &wiperParams : nullptr );
    }
    if ( bWipersSwinging ) {
        const Matrix4x4f nodeT = wiperNode >= 0 ? car.getNodeTransform( wiperNode ) : Matrix4x4f();
        const Vector3f axis( 0.0f, 0.557f, 0.830f ); // the Delta's 34 degree rake [INF]
        for ( uint32_t h = 0; h < wipers.size(); h++ ) {
            const WiperHalf &wiper = wipers[h];
            const float angle = ( h == 0 ? 1.0f : -1.0f ) * wiperAngle;
            const Matrix4x4f swing = Matrix4x4f::Translation( wiper.Pivot ) *
                                     Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( axis, angle ) ) *
                                     Matrix4x4f::Translation( Vector3f( -wiper.Pivot.x, -wiper.Pivot.y, -wiper.Pivot.z ) );
            renderer.draw( wiper.Mesh, car.getMaterials()[car.getItems()[static_cast<size_t>( wiper.ItemIndex )].MaterialIndex],
                           world * nodeT * swing );
        }
    }
    if ( bInterior ) {
        interior.draw( renderer, world );
    }
    for ( int32_t node : wheelNodes ) {
        // D_RIM_xx: rim size (scale) and side (right wheels turned by 180
        // degrees); the next node (?_TIRE_xx) places the tire. The axle letter
        // is the character after the last '_' ("D_RIM_FR"): a plain
        // find("_R") matches every name (they all contain "D_RIM_") and made
        // all four wheels use the rear rim / diameter.
        const std::string &nodeName = car.getScene().getNodes()[node].Name;
        const size_t axle = nodeName.rfind( '_' );
        const bool bRear = axle != std::string::npos && axle + 1 < nodeName.size() && nodeName[axle + 1] == 'R';
        const EdenModel &wheelRim = ( bRear && rimRear.isLoaded() ) ? rimRear : rim;
        static const bool bNoRim = CmdLine::HasArg( "-norim" );
        if ( wheelRim.isLoaded() && !bNoRim ) {
            // Rim meshes are normalized (radius 0.212 m). The baked D_RIM_xx
            // radial scale is the database diameter (+ 2.2 cm of lip) over
            // that radius (the California bakes 1.24 for its 19" rims); a
            // tuning shop rim size replaces it (the multiplier divides the
            // baked scale out), and a scene with no baked radius (scale 1)
            // gets it from the diameter too.
            Matrix4x4f rimTransform = car.getNodeTransform( node );
            const Vector3f up( rimTransform.m[1][0], rimTransform.m[1][1], rimTransform.m[1][2] );
            float radialScale = 1.0f;
            const int32_t diameter = bRear ? rimDiameterRear : rimDiameterFront;
            const float bakedScale = up.length();
            if ( bakedScale > 1e-4f ) {
                const float bakedDiameter = ( bakedScale * kRimMeshRadius - kRimLip ) * 2.0f / 0.0254f;
                if ( std::abs( bakedDiameter - static_cast<float>( diameter ) ) > 0.25f ) {
                    radialScale = ( ( diameter * 0.0254f * 0.5f + kRimLip ) / kRimMeshRadius ) / bakedScale;
                    // Per car (the showrooms draw 8 cars a frame; a process-wide
                    // guard re-logged for every car whose diameter differed).
                    int32_t &logged = bRear ? loggedRimRear : loggedRimFront;
                    if ( logged != diameter ) {
                        logged = diameter;
                        OTDU_LOG_INFO( "CinematicCar: rim %d\" drawn at lip %.3f m (multiplier %.3f on the baked %.3f)\n",
                                       diameter, diameter * 0.0254f * 0.5f + kRimLip, radialScale, bakedScale );
                    }
                }
            }
            rimTransform = rimTransform * Matrix4x4f::Scale( Vector3f( 1.0f, radialScale, radialScale ) );
            wheelRim.draw( renderer, world * rimTransform );
        }
        const int32_t tireNode = bRear ? tireNodeRear : tireNodeFront;
        static const bool bNoTire = CmdLine::HasArg( "-notire" );
        if ( tireNode >= 0 && !bNoTire ) {
            tire.drawNode( renderer, tireNode, world * car.getNodeTransform( node + 1 ) );
        }
    }
}

void CinematicCar::applySurfaceState( float mud, float dirt, float hardness, float wetness ) {
    // The crust (hardness) drives the CarDirt wash law; the paint only needs
    // how much stain there is and how wet the body is.
    (void)hardness;
    if ( !car.isLoaded() ) {
        return;
    }
    const float stain = Clamp( Max( mud, dirt ), 0.0f, 1.0f );
    const float wet = Clamp( wetness, 0.0f, 1.0f );
    // The paint did not visibly move: skip the material walk (draw() runs
    // several times a frame for the cube / shadow / mirror passes).
    if ( paintBaselineValid && std::abs( stain - stainVisual ) < 0.004f && std::abs( wet - wetVisual ) < 0.004f ) {
        return;
    }
    stainVisual = stain;
    wetVisual = wet;
    static const uint64_t kRampClr1 = GetIdentifier64bit( "RAMPCLR1" );
    static const uint64_t kRampClr2 = GetIdentifier64bit( "RAMPCLR2" );
    const auto isPaint = []( const std::string &name ) {
        return name == "Paint" || name == "Paint_SK" || name == "Paint_LR" || name == "Paint_MR" ||
               name == "Paint_N" || name == "Paint2" || name == "Paint2_SK";
    };
    if ( !paintBaselineValid ) {
        // Capture the clean values once (and again after applyDatabasePaint):
        // every later call interpolates from these, never from the tinted ones.
        paintBaseline.clear();
        const std::vector<EdenMaterial> &materials = car.getMaterials();
        for ( size_t i = 0; i < materials.size(); i++ ) {
            if ( !isPaint( materials[i].Name ) ) {
                continue;
            }
            PaintBaseline baseline;
            baseline.Material = static_cast<uint32_t>( i );
            baseline.Diffuse = materials[i].Diffuse;
            baseline.Ambient = materials[i].Ambient;
            baseline.Specular = materials[i].Specular;
            auto it = materials[i].Parameters.find( kRampClr1 );
            if ( it != materials[i].Parameters.end() ) {
                baseline.RampClr1 = it->second;
                baseline.bRampClr1 = true;
            }
            it = materials[i].Parameters.find( kRampClr2 );
            if ( it != materials[i].Parameters.end() ) {
                baseline.RampClr2 = it->second;
                baseline.bRampClr2 = true;
            }
            paintBaseline.push_back( baseline );
        }
        paintBaselineValid = true;
    }
    // Mud is a dark, matte brown; dry dust is the pale grey-brown of the
    // island's dirt roads. A wet film darkens the paint a touch and the draw()
    // gloss term mirrors it.
    const Vector4f mudTint( 0.33f, 0.25f, 0.17f, 1.0f );
    const Vector4f dustTint( 0.46f, 0.41f, 0.34f, 1.0f );
    const float mudShare = stain > 0.001f ? Clamp( mud / stain, 0.0f, 1.0f ) : 0.0f;
    const float blend = Clamp( stain * 0.85f, 0.0f, 0.88f );
    const float darkness = 1.0f - 0.22f * wet; // a soaked body reads darker
    const Vector4f tint( Lerp1( dustTint.x, mudTint.x, mudShare ), Lerp1( dustTint.y, mudTint.y, mudShare ),
                         Lerp1( dustTint.z, mudTint.z, mudShare ), 1.0f );
    std::vector<EdenMaterial> &materials = car.getMaterials();
    for ( PaintBaseline &baseline : paintBaseline ) {
        EdenMaterial &material = materials[baseline.Material];
        if ( baseline.bRampClr1 ) {
            Vector4f value = baseline.RampClr1;
            value.x = Lerp1( value.x, tint.x, blend ) * darkness;
            value.y = Lerp1( value.y, tint.y, blend ) * darkness;
            value.z = Lerp1( value.z, tint.z, blend ) * darkness;
            material.Parameters[kRampClr1] = value;
        }
        if ( baseline.bRampClr2 ) {
            Vector4f value = baseline.RampClr2;
            value.x = Lerp1( value.x, tint.x * 0.8f, blend ) * darkness;
            value.y = Lerp1( value.y, tint.y * 0.8f, blend ) * darkness;
            value.z = Lerp1( value.z, tint.z * 0.8f, blend ) * darkness;
            material.Parameters[kRampClr2] = value;
        }
        // A car whose paint has no RampClr constants (a plain diffuse .2dm):
        // tint the diffuse / ambient and kill the specular as it gets dirty.
        if ( !baseline.bRampClr1 && !baseline.bRampClr2 ) {
            material.Diffuse =
                Vector4f( Lerp1( baseline.Diffuse.x, tint.x, blend ) * darkness,
                          Lerp1( baseline.Diffuse.y, tint.y, blend ) * darkness,
                          Lerp1( baseline.Diffuse.z, tint.z, blend ) * darkness, baseline.Diffuse.w );
            material.Ambient =
                Vector4f( Lerp1( baseline.Ambient.x, tint.x, blend ) * darkness,
                          Lerp1( baseline.Ambient.y, tint.y, blend ) * darkness,
                          Lerp1( baseline.Ambient.z, tint.z, blend ) * darkness, baseline.Ambient.w );
            const float specular = 1.0f - 0.8f * blend;
            material.Specular = Vector4f( baseline.Specular.x * specular, baseline.Specular.y * specular,
                                          baseline.Specular.z * specular, baseline.Specular.w );
        }
    }
}

