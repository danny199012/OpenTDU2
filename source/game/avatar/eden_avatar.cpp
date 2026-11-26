#include "shared.h"
#include "eden_avatar.h"

#include "core/xmb.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "game/player_profile.h"
#include "system/cmd_line.h"
#include "render/gs_render.h"
#include "render/texture_cache.h"
#include "render/2db.h"
#include "render/image_file.h"
#include "tattoo.h"
#include "render/3dg.h"

namespace {
constexpr uint64_t kHairShaderSet = 0xd5013ed0dbdededeull;

// FUN_00cb43e0: BodyID -> body mesh, body material variant, head.
struct BodyEntry {
    const char *pBody;
    const char *pMaterials;
    const char *pHead;
};
const BodyEntry kBodies[6] = {
    { "man_body", "man_body", "man_head_occ" },           { "woman_body", "woman_body", "woman_head_occ" },
    { "man_body", "man_body_asia", "man_head_asia" },     { "woman_body", "woman_body_asia", "woman_head_asia" },
    { "man_body", "man_body_afrik", "man_head_afrik" },   { "woman_body", "woman_body_afrik", "woman_head_afrik" },
};

// Body zones (FUN_00cada30, material = fold of the full name) and the
// matching DBClothe flags (0: the garment covers the zone).
const char *kZoneMaterials[21] = { "BASE",
                                   "BOTTOM_FEET",
                                   "BOTTOM_ANKLES",
                                   "BOTTOM_CALVES",
                                   "BOTTOM_KNEES",
                                   "BOTTOM_THIGHS",
                                   "BOTTOM_PELVIS",
                                   "TOP_LOWER_ABDOMEN",
                                   "TOP_ABDOMEN",
                                   "TOP_LOWER_BACK",
                                   "TOP_BACK",
                                   "TOP_UPPER_BACK_CENTER",
                                   "TOP_UPPER_BACK_SIDES",
                                   "TOP_CLEAVAGE_LARGE",
                                   "TOP_CLEAVAGE",
                                   "TOP_CLEAVAGE_SIDES",
                                   "TOP_UPPER_CHEST",
                                   "TOP_ARMS_SHOULDERS",
                                   "TOP_ARMS",
                                   "TOP_ARMS_FOREARMS",
                                   "TOP_ARMS_HANDS" };
const char *kZoneFlags[21] = { "_base",       "_feet",          "_ankles",           "_calves",
                               "_knees",      "_thighs",        "_pelvis",           "_lower_abdomen",
                               "_abdomen",    "_lower_back",    "_back",             "_upper_back_center",
                               "_upper_back_sides", "_cleavage_large", "_cleavage", "_cleavage_sides",
                               "_upper_chest", "_shoulders",    "_arms",             "_forearms",
                               "_hands" };

const char *kSlots[10] = { "hair", "top", "bottom", "dress", "shoes", "glasses", "gloves", "helmet", "accessories",
                           "beard" };

// Clothes directory from the code of the file name (W_PN_..., M_HR_...).
const char *ClothesDirectory( const std::string &file ) {
    const std::string code = file.size() >= 4 ? ToUpper( file.substr( 2, 2 ) ) : "";
    static const std::pair<const char *, const char *> kDirectories[] = {
        { "HR", "hair" },       { "PN", "pnj" },      { "SP", "sportswear" }, { "SW", "streetwear" },
        { "FH", "fashion" },    { "HL", "highlife" }, { "CA", "casino" },     { "ED", "eden" },
    };
    for ( const auto &entry : kDirectories ) {
        if ( code == entry.first ) {
            return entry.second;
        }
    }
    return nullptr;
}

// .avt: big endian tree { u16 size; u16 key; value[size - 2] }, BodyID key
// 0xcd26 (u16).
bool FindAvtValue( const uint8_t *pData, size_t begin, size_t end, uint16_t wantedKey, uint16_t &outValue ) {
    size_t offset = begin;
    while ( offset + 4 <= end ) {
        const uint16_t entrySize = static_cast<uint16_t>( ( pData[offset] << 8 ) | pData[offset + 1] );
        const uint16_t key = static_cast<uint16_t>( ( pData[offset + 2] << 8 ) | pData[offset + 3] );
        if ( entrySize < 2 ) {
            return false;
        }
        const size_t valueSize = entrySize - 2u;
        if ( offset + 4 + valueSize > end ) {
            return false;
        }
        if ( key == wantedKey && valueSize == 2 ) {
            outValue = static_cast<uint16_t>( ( pData[offset + 4] << 8 ) | pData[offset + 5] );
            return true;
        }
        if ( valueSize > 16 && FindAvtValue( pData, offset + 4, offset + 4 + valueSize, wantedKey, outValue ) ) {
            return true;
        }
        offset += 4 + valueSize;
    }
    return false;
}

// Raw value bytes of a key (big endian tree as above).
bool FindAvtBytes( const uint8_t *pData, size_t begin, size_t end, uint16_t wantedKey, const uint8_t *&outValue,
                   size_t &outSize ) {
    size_t offset = begin;
    while ( offset + 4 <= end ) {
        const uint16_t entrySize = static_cast<uint16_t>( ( pData[offset] << 8 ) | pData[offset + 1] );
        const uint16_t key = static_cast<uint16_t>( ( pData[offset + 2] << 8 ) | pData[offset + 3] );
        if ( entrySize < 2 ) {
            return false;
        }
        const size_t valueSize = entrySize - 2u;
        if ( offset + 4 + valueSize > end ) {
            return false;
        }
        if ( key == wantedKey ) {
            outValue = pData + offset + 4;
            outSize = valueSize;
            return true;
        }
        if ( valueSize > 16 && FindAvtBytes( pData, offset + 4, offset + 4 + valueSize, wantedKey, outValue, outSize ) ) {
            return true;
        }
        offset += 4 + valueSize;
    }
    return false;
}

// Big endian f32 values of a key (count read, 0 if absent).
uint32_t FindAvtFloats( const BankEntry *pAvt, uint16_t key, float *pOut, uint32_t maxCount ) {
    if ( pAvt == nullptr || pAvt->Size <= 2 ) {
        return 0;
    }
    const uint8_t *pValue = nullptr;
    size_t size = 0;
    if ( !FindAvtBytes( pAvt->pData, 2, pAvt->Size, key, pValue, size ) ) {
        return 0;
    }
    uint32_t count = 0;
    for ( ; count < maxCount && ( count + 1 ) * 4 <= size; count++ ) {
        const uint32_t bits = ( static_cast<uint32_t>( pValue[count * 4] ) << 24 ) | ( pValue[count * 4 + 1] << 16 ) |
                              ( pValue[count * 4 + 2] << 8 ) | pValue[count * 4 + 3];
        memcpy( &pOut[count], &bits, 4 );
    }
    return count;
}

XmbValue FindRow( const char *pTable, const char *pField, const std::function<bool( XmbValue )> &match ) {
    if ( gpDatabase == nullptr ) {
        return XmbValue();
    }
    XmbValue root = gpDatabase->getDocument().getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( pTable ); i++ ) {
        XmbValue row = root.getArrayElement( pTable, i );
        if ( match( row[pField] ) ) {
            return row;
        }
    }
    return XmbValue();
}
} // namespace

EdenAvatarLibrary::EdenAvatarLibrary() {}

EdenAvatarLibrary::~EdenAvatarLibrary() { unload(); }

bool EdenAvatarLibrary::load() {
    unload();
    const std::string folder = gpFile->getBankFolder() + "avatar/";
    TextureCache &textures = gpRender->getTextureCache();
    // COMMON_BODIES (hires), COMMON_BODIES_LIGHT as the fallback (FUN_00ca9040).
    if ( !bodiesBank.load( ( folder + "common_bodies.bnk" ).c_str() ) &&
         !bodiesBank.load( ( folder + "common_bodies_light.bnk" ).c_str() ) ) {
        OTDU_LOG_WARN( "Avatars: common_bodies.bnk not found\n" );
        return false;
    }
    textures.loadBankTextures( bodiesBank, bodiesBank.getPath().c_str() );
    if ( clothesCommonBank.load( ( folder + "clothes/common_clothes.bnk" ).c_str() ) ) {
        textures.loadBankTextures( clothesCommonBank, clothesCommonBank.getPath().c_str() );
        if ( const BankEntry *pPalette = clothesCommonBank.findEntry( "hair_mat", "2dm" ) ) {
            hairPalette.parse( pPalette->pData, pPalette->Size );
        }
    }
    configBank.load( ( folder + "config/pnj.bnk" ).c_str() );
    const BankEntry *pMan = bodiesBank.findEntry( "man_body", "xsb" );
    const BankEntry *pWoman = bodiesBank.findEntry( "woman_body", "xsb" );
    bLoaded = pMan != nullptr && pWoman != nullptr && manSkeleton.parse( pMan->pData, pMan->Size ) &&
              womanSkeleton.parse( pWoman->pData, pWoman->Size );
    OTDU_LOG_INFO( "Avatars: skeletons %u / %u bones\n", manSkeleton.getNumBones(), womanSkeleton.getNumBones() );
    return bLoaded;
}

void EdenAvatarLibrary::unload() {
    if ( gpRender != nullptr ) {
        TextureCache &textures = gpRender->getTextureCache();
        for ( Bank *pBank : { &bodiesBank, &clothesCommonBank } ) {
            if ( pBank->isLoaded() ) {
                textures.releaseOwner( pBank->getPath().c_str() );
            }
        }
        for ( auto &entry : clothesBanks ) {
            if ( entry.second != nullptr && entry.second->isLoaded() ) {
                textures.releaseOwner( entry.second->getPath().c_str() );
            }
        }
    }
    clothesBanks.clear();
    bodiesBank.unload();
    clothesCommonBank.unload();
    configBank.unload();
    bLoaded = false;
}

const Bank *EdenAvatarLibrary::getClothesBank( const std::string &file ) {
    auto it = clothesBanks.find( file );
    if ( it != clothesBanks.end() ) {
        return it->second.get();
    }
    const char *pDirectory = ClothesDirectory( file );
    std::unique_ptr<Bank> pBank;
    if ( pDirectory != nullptr ) {
        const std::string path =
            gpFile->getBankFolder() + "avatar/clothes/" + pDirectory + "/" + ToLower( file ) + ".bnk";
        pBank = std::make_unique<Bank>();
        if ( pBank->load( path.c_str() ) ) {
            gpRender->getTextureCache().loadBankTextures( *pBank, pBank->getPath().c_str() );
        } else {
            OTDU_LOG_WARN( "Avatars: clothes bank '%s' not found\n", path.c_str() );
            pBank.reset();
        }
    }
    const Bank *pResult = pBank.get();
    clothesBanks[file] = std::move( pBank );
    return pResult;
}

// The player's own avatar: the profile's AvatarConfig; a profile that never picked one (-gm FREERIDE straight in) is
// the default the rooms fall back to, so its saved looks / edits still apply.
static bool IsPlayerConfig( const char *pConfigId ) {
    const std::string &config = GetPlayerProfile().AvatarConfig;
    return StringCmpI( config.empty() ? "AVT_Male_Intro" : config.c_str(), pConfigId ) == 0;
}

EdenAvatar::EdenAvatar() {}

EdenAvatar::~EdenAvatar() { unload(); }

void EdenAvatar::unload() {
    // the materials die with the parts; the composited tattoo skins are this avatar's own
    parts.clear();
    if ( gpRender != nullptr ) {
        for ( GPUTexture *pTexture : tattooTextures ) {
            gpRender->getTextureCache().destroyTexture( pTexture );
        }
    }
    tattooTextures.clear();
    skinSlots.clear(); // pointed into the parts' materials
    pickMesh = PickMesh();
    headParts.clear();
    pSkeleton = nullptr;
}

bool EdenAvatarLibrary::readPresetFace( const char *pConfigId, int8_t outParams[49], float &outAge, float &outFreckle ) {
    memset( outParams, 0, 49 );
    outAge = 0.0f;
    outFreckle = 0.0f;
    if ( !bLoaded ) {
        return false;
    }
    XmbValue pnj = FindRow( "DBPNJ", "_configid",
                            [pConfigId]( XmbValue v ) { return StringCmpI( v.asString().c_str(), pConfigId ) == 0; } );
    if ( !pnj.isValid() ) {
        return false;
    }
    const BankEntry *pAvt = configBank.findEntry( pnj["_fileconfigid"].asString().c_str(), "avt" );
    if ( pAvt == nullptr || pAvt->Size <= 2 ) {
        return false;
    }
    FindAvtFloats( pAvt, 0x8d6d, &outAge, 1 );
    FindAvtFloats( pAvt, 0x3f89, &outFreckle, 1 );
    const uint8_t *pValue = nullptr;
    size_t valueSize = 0;
    if ( FindAvtBytes( pAvt->pData, 2, pAvt->Size, 0x9d93, pValue, valueSize ) ) {
        size_t offset = 0;
        for ( uint32_t i = 0; i < 49 && offset + 5 <= valueSize; i++ ) {
            const uint16_t entrySize = static_cast<uint16_t>( ( pValue[offset] << 8 ) | pValue[offset + 1] );
            if ( entrySize != 3 ) {
                break;
            }
            outParams[i] = static_cast<int8_t>( pValue[offset + 4] );
            offset += 5;
        }
    }
    return true;
}

bool EdenAvatar::load( EdenAvatarLibrary &library, const char *pConfigId ) {
    unload();
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    if ( !library.isLoaded() || pRenderer == nullptr ) {
        return false;
    }
    XmbValue pnj = FindRow( "DBPNJ", "_configid",
                            [pConfigId]( XmbValue v ) { return StringCmpI( v.asString().c_str(), pConfigId ) == 0; } );
    if ( !pnj.isValid() ) {
        OTDU_LOG_WARN( "Avatars: DBPNJ '%s' not found\n", pConfigId );
        return false;
    }
    bWoman = pnj["_sex"].asInt() == 1;
    // The cosmetic clinic's edits belong to the player's own config only.
    bPlayerFace = IsPlayerConfig( pConfigId ) && !CmdLine::HasArg( "-nosurgery" );
    // .avt BodyID (default: occidental body of the sex).
    uint16_t bodyId = bWoman ? 1 : 0;
    const std::string avtName = pnj["_fileconfigid"].asString();
    if ( const BankEntry *pAvt = library.getConfigBank().findEntry( avtName.c_str(), "avt" ) ) {
        uint16_t value = 0;
        if ( pAvt->Size > 2 && FindAvtValue( pAvt->pData, 2, pAvt->Size, 0xcd26, value ) && value < 6 ) {
            bodyId = value;
        }
    }
    pSkeleton = &library.getSkeleton( ( bodyId & 1 ) != 0 );
    const BodyEntry &body = kBodies[bodyId];

    // Clothes rows (and the zones they cover).
    bool zoneVisible[21];
    for ( bool &bVisible : zoneVisible ) {
        bVisible = true;
    }
    struct Worn {
        std::string File;
        int32_t Color;
        bool bHair;
        bool bHead; // worn on the head (hair, beard, glasses, helmet)
    };
    std::vector<Worn> worn;
    // The player's own config wears the clothes picked in a house closet
    // (PlayerProfile::WornClothes, _parttype -> _id_num): the slot's row is
    // replaced. The slot <-> _parttype pairs are FUN_00cb2f30's
    // (skin_zones_spec.md 2.3) [CODE].
    const PlayerProfile &profile = GetPlayerProfile();
    const bool bPlayer = !profile.WornClothes.empty() && IsPlayerConfig( pConfigId ) &&
                         !CmdLine::HasArg( "-noworn" );
    if ( CmdLine::HasArg( "-logworn" ) ) {
        OTDU_LOG_INFO( "Avatars: %s player %d, %zu worn, config '%s'\n", pConfigId, bPlayer ? 1 : 0, profile.WornClothes.size(),
                       profile.AvatarConfig.c_str() );
    }
    auto partTypeOf = []( const char *pSlot ) -> int32_t {
        static const struct {
            const char *pSlot;
            int32_t PartType;
        } kParts[] = { { "bottom", 1 }, { "glasses", 2 }, { "hair", 3 },  { "gloves", 4 },       { "helmet", 5 },
                       { "shoes", 6 },  { "top", 7 },     { "dress", 8 }, { "beard", 9 }, { "accessories", 10 } };
        for ( const auto &part : kParts ) {
            if ( strcmp( part.pSlot, pSlot ) == 0 ) {
                return part.PartType;
            }
        }
        return 0;
    };
    for ( const char *pSlot : kSlots ) {
        const uint64_t clotheHash = pnj[( std::string( "_clothe_" ) + pSlot ).c_str()].asUInt64();
        XmbValue clothe = FindRow( "DBClothe", "_hashcode", [clotheHash]( XmbValue v ) { return v.asUInt64() == clotheHash; } );
        if ( bPlayer ) {
            auto it = profile.WornClothes.find( partTypeOf( pSlot ) );
            if ( it != profile.WornClothes.end() ) {
                const int64_t id = it->second;
                XmbValue picked = FindRow( "DBClothe", "_id_num", [id]( XmbValue v ) { return v.asInt() == id; } );
                if ( picked.isValid() ) {
                    clothe = picked;
                    OTDU_LOG_INFO( "Avatars: %s wears closet item %lld '%s' on '%s'\n", pConfigId,
                                   static_cast<long long>( id ), picked["_file"].asString().c_str(), pSlot );
                }
            }
        }
        if ( !clothe.isValid() ) {
            continue;
        }
        // FUN_00cac1b0: only the top, bottom, dress, shoes and gloves slots
        // hide body zones (whatever their file).
        const bool bZoneSlot = strcmp( pSlot, "top" ) == 0 || strcmp( pSlot, "bottom" ) == 0 ||
                               strcmp( pSlot, "dress" ) == 0 || strcmp( pSlot, "shoes" ) == 0 ||
                               strcmp( pSlot, "gloves" ) == 0;
        if ( bZoneSlot ) {
            for ( uint32_t z = 0; z < 21; z++ ) {
                if ( clothe[kZoneFlags[z]].asInt( 1 ) == 0 ) {
                    zoneVisible[z] = false;
                }
            }
        }
        std::string file = clothe["_file"].asString();
        if ( file.empty() || file.find( "NO_" ) != std::string::npos ) {
            continue;
        }
        const bool bHead = strcmp( pSlot, "hair" ) == 0 || strcmp( pSlot, "beard" ) == 0 ||
                           strcmp( pSlot, "glasses" ) == 0 || strcmp( pSlot, "helmet" ) == 0;
        if ( !bHead ) {
            // The slot rows can carry the other sex's variant of the garment
            // (AVT_Male's top / bottom / shoes point at the W_ED_*_Groom_01
            // rows, DBClothe _sex 1 [DATA]): the avatar's sex picks the file
            // prefix — the per-sex banks pair (m_ed_*_groom_01 /
            // w_ed_*_groom_01). Without the swap the male body wears the
            // woman's shirt mesh and pokes through it (the "z-fighting"
            // chest / shoulders of the valet livery). Head wear (the hair
            // W_HR_R_Jane on the male) resolves as-is [INF: the swap rule is
            // not in the readable code; the rendered hair is correct without
            // it].
            std::string swapped = file;
            if ( !bWoman && swapped.compare( 0, 2, "W_" ) == 0 ) {
                swapped[0] = 'M';
            } else if ( bWoman && swapped.compare( 0, 2, "M_" ) == 0 ) {
                swapped[0] = 'W';
            }
            if ( swapped != file && library.getClothesBank( swapped ) != nullptr ) {
                file = swapped;
            }
        }
        int32_t slotColor = static_cast<int32_t>( pnj[( std::string( "_" ) + pSlot + "_color" ).c_str()].asInt() );
        if ( bPlayer && strcmp( pSlot, "hair" ) == 0 && profile.HairColor >= 0 ) {
            slotColor = profile.HairColor; // the hairdresser's tint
        } else if ( bPlayer && strcmp( pSlot, "beard" ) == 0 && profile.BeardColor >= 0 ) {
            slotColor = profile.BeardColor;
        }
        worn.push_back( { file, slotColor,
                          strcmp( pSlot, "hair" ) == 0 || strcmp( pSlot, "beard" ) == 0, bHead } );
    }

    // Body (material variant of the BodyID) and head.
    const Bank &bodies = library.getBodiesBank();
    auto loadPart = [&]( const Bank &bank, const char *pMesh, const char *pMaterials ) -> EdenModel * {
        const BankEntry *pScene = bank.findEntry( pMesh, "3dd" );
        const BankEntry *pGeometry = bank.findEntry( pMesh, "3dg" );
        const BankEntry *pMaterialEntry = bank.findEntry( pMaterials, "2dm" );
        auto pModel = std::make_unique<EdenModel>();
        if ( pScene == nullptr || pGeometry == nullptr ||
             !pModel->load( *pRenderer, *pScene, *pGeometry, pMaterialEntry, nullptr, bank.getPath().c_str() ) ) {
            OTDU_LOG_WARN( "Avatars: part '%s' not loaded\n", pMesh );
            return nullptr;
        }
        parts.push_back( std::move( pModel ) );
        return parts.back().get();
    };
    if ( EdenModel *pBody = loadPart( bodies, body.pBody, body.pMaterials ) ) {
        for ( EdenMaterial &material : pBody->getMaterials() ) {
            for ( uint32_t z = 0; z < 21; z++ ) {
                if ( !zoneVisible[z] && !CmdLine::HasArg( "-nudeavatar" ) && material.Hashcode == GetIdentifier64bit( kZoneMaterials[z] ) ) {
                    material.pProgram = nullptr; // hidden zone
                }
            }
        }
    }
    if ( loadPart( bodies, body.pHead, body.pHead ) != nullptr ) {
        headParts.push_back( parts.size() - 1 );
    }
    applySkin( library.getConfigBank().findEntry( avtName.c_str(), "avt" ) );
    if ( bPlayerFace || CmdLine::HasArg( "-dumpbodyuv" ) ) {
        // -tattoo "design,u,v,scale,rot,opacity,rrggbb;..." overrides the profile for a run (tests).
        const char *pTattooArg = CmdLine::GetArgValue( "-tattoo" );
        tattooDecals = ParseTattoos( pTattooArg != nullptr ? pTattooArg : GetPlayerProfile().Tattoos );
        applyTattoos( bodies );
        // -tattoopick "ox,oy,oz,dx,dy,dz;...": aim-to-place test, rays in model space (bind pose); each hit gets a star.
        if ( const char *pPick = CmdLine::GetArgValue( "-tattoopick" ) ) {
            std::vector<TattooDecal> placed = tattooDecals;
            std::string rays = pPick;
            size_t at = 0;
            while ( at < rays.size() ) {
                size_t end = rays.find( ';', at );
                end = end == std::string::npos ? rays.size() : end;
                float r[6] = {};
                if ( sscanf( rays.substr( at, end - at ).c_str(), "%f,%f,%f,%f,%f,%f", &r[0], &r[1], &r[2], &r[3], &r[4], &r[5] ) == 6 ) {
                    float u = 0.0f, v = 0.0f;
                    const bool bHit = pickSkin( Vector3f( r[0], r[1], r[2] ), Vector3f( r[3], r[4], r[5] ), Matrix4x4f::Identity(), {}, u, v );
                    OTDU_LOG_INFO( "Tattoo pick: ray %s -> %s u %.3f v %.3f\n", rays.substr( at, end - at ).c_str(), bHit ? "hit" : "miss", u, v );
                    if ( bHit ) {
                        TattooDecal decal;
                        decal.Design = "star";
                        decal.U = u;
                        decal.V = v;
                        decal.Scale = 0.04f;
                        decal.Color = placed.size() % 2 == 0 ? 0xff0000 : 0x0000ff;
                        placed.push_back( decal );
                    }
                }
                at = end + 1;
            }
            setTattoos( placed );
        }
    }
    // FaceParamArray (FUN_00cbd030: memcpy of 0x31 bytes to +0xa2): a list
    // of 49 child nodes {u16 3, u16 key, u8 value}, in order [INF: element
    // order = file order].
    memset( faceParams, 0, sizeof( faceParams ) );
    if ( const BankEntry *pAvt = library.getConfigBank().findEntry( avtName.c_str(), "avt" ) ) {
        const uint8_t *pValue = nullptr;
        size_t valueSize = 0;
        if ( pAvt->Size > 2 && FindAvtBytes( pAvt->pData, 2, pAvt->Size, 0x9d93, pValue, valueSize ) ) {
            size_t offset = 0;
            for ( uint32_t i = 0; i < 49 && offset + 5 <= valueSize; i++ ) {
                const uint16_t entrySize = static_cast<uint16_t>( ( pValue[offset] << 8 ) | pValue[offset + 1] );
                if ( entrySize != 3 ) {
                    break;
                }
                faceParams[i] = static_cast<int8_t>( pValue[offset + 4] );
                offset += 5;
            }
        }
    }
    if ( bPlayerFace && GetPlayerProfile().FaceParams.size() == 49 ) {
        memcpy( faceParams, GetPlayerProfile().FaceParams.data(), sizeof( faceParams ) );
    }

    for ( const Worn &item : worn ) {
        if ( CmdLine::HasArg( "-nudeavatar" ) ) { // debug: skin only (tattoo region mapping)
            break;
        }
        const Bank *pBank = library.getClothesBank( item.File );
        if ( pBank == nullptr ) {
            continue;
        }
        const std::string base = ToLower( item.File );
        EdenModel *pModel = loadPart( *pBank, base.c_str(), base.c_str() );
        if ( pModel == nullptr ) {
            continue;
        }
        if ( item.bHead ) {
            headParts.push_back( parts.size() - 1 );
        }
        if ( item.bHair ) {
            // FUN_00cb2080: hair colour index -> HAIR_MAT palette material
            // (14 = keep the hair's own colours).
            const auto &palette = library.getHairPalette().getOrderedMaterials();
            if ( item.Color >= 0 && item.Color < static_cast<int32_t>( palette.size() ) ) {
                const MaterialDesc &colors = palette[item.Color];
                for ( EdenMaterial &material : pModel->getMaterials() ) {
                    if ( material.ShaderSetGUID != kHairShaderSet ) {
                        continue;
                    }
                    for ( const char *pName : { "PRIMCLR", "SCDCLR" } ) {
                        auto it = colors.Parameters.find( GetIdentifier64bit( pName ) );
                        if ( it != colors.Parameters.end() ) {
                            material.Parameters[it->first] = it->second;
                        }
                    }
                }
            }
            continue;
        }
        // Colour variant #index of <file>_mat.2dm.
        const BankEntry *pVariants = pBank->findEntry( ( base + "_mat" ).c_str(), "2dm" );
        if ( pVariants == nullptr ) {
            continue;
        }
        Render2DM variants;
        variants.parse( pVariants->pData, pVariants->Size );
        for ( EdenMaterial &material : pModel->getMaterials() ) {
            int32_t index = 0;
            for ( const MaterialDesc &variant : variants.getOrderedMaterials() ) {
                if ( variant.Hashcode != material.Hashcode ) {
                    continue;
                }
                if ( index++ == item.Color ) {
                    for ( const auto &parameter : variant.Parameters ) {
                        material.Parameters[parameter.first] = parameter.second;
                    }
                    break;
                }
            }
        }
    }
    OTDU_LOG_DEBUG( "Avatars: '%s' body %u, %zu parts\n", pConfigId, bodyId, parts.size() );
    return !parts.empty();
}

void EdenAvatar::draw( EdenRenderer &renderer, const Matrix4x4f &world,
                       const std::vector<Matrix4x4f> &skinMatrices, const EdenAvatarLighting *pLighting,
                       bool bDrawHead ) const {
    for ( size_t i = 0; i < parts.size(); i++ ) {
        if ( !bDrawHead && std::find( headParts.begin(), headParts.end(), i ) != headParts.end() ) {
            continue;
        }
        parts[i]->drawSkinned( renderer, world, skinMatrices, pLighting );
    }
}

void EdenAvatar::setFacialCoefficients( const Vector4f &coef1, const Vector4f &coef2 ) {
    static const uint64_t kCoef1 = GetIdentifier64bit( "facialAnimCoef1" ); // 0x4e72928698868e8f
    static const uint64_t kCoef2 = GetIdentifier64bit( "facialAnimCoef2" ); // 0x4e73928698868e8f
    for ( auto &pPart : parts ) {
        for ( EdenMaterial &material : pPart->getMaterials() ) {
            if ( material.ShaderSetGUID == 0xd5013ed0dbcddbcaull ) {
                material.Parameters[kCoef1] = coef1;
                material.Parameters[kCoef2] = coef2;
            }
        }
    }
}

void EdenAvatar::setFaceEdit( const int8_t *pParams, float age, float freckle ) {
    memcpy( faceParams, pParams, sizeof( faceParams ) );
    const uint64_t specParmId = GetIdentifier64bit( "SPECPARM" );
    const uint64_t coeffsId = GetIdentifier64bit( "COEFFS" );
    for ( auto &pPart : parts ) {
        for ( EdenMaterial &material : pPart->getMaterials() ) {
            if ( material.ShaderSetGUID != 0xd5013ed0dbcdcdcdull && material.ShaderSetGUID != 0xd5013ed0dbcddbcaull ) {
                continue;
            }
            material.Parameters[specParmId].w = age;
            material.Parameters[coeffsId].y = freckle;
        }
    }
}

void EdenAvatar::applySkin( const BankEntry *pAvt ) {
    // .avt keys (FUN_00cbd030): 6cb0 SkinAmbiant, 1a6c SkinDiffuse, e173
    // SkinSpecular, cbfd (SpecCoeff, SpecPow, RIMCoeff), e9ce (DiffWrap,
    // ScatteringCoeff), 8d6d age, 3f89 freckle. Defaults (FUN_00cbd680):
    // colours 1, age 0, freckle 0; Spec/Wrap from the 2DM.
    float ambient[3] = { 1.0f, 1.0f, 1.0f };
    float diffuse[3] = { 1.0f, 1.0f, 1.0f };
    float specular[3] = { 1.0f, 1.0f, 1.0f };
    float spec[3] = {};
    float wrap[2] = {};
    float age = 0.0f;
    float freckle = 0.0f;
    FindAvtFloats( pAvt, 0x6cb0, ambient, 3 );
    FindAvtFloats( pAvt, 0x1a6c, diffuse, 3 );
    FindAvtFloats( pAvt, 0xe173, specular, 3 );
    const uint32_t numSpec = FindAvtFloats( pAvt, 0xcbfd, spec, 3 );
    const uint32_t numWrap = FindAvtFloats( pAvt, 0xe9ce, wrap, 2 );
    FindAvtFloats( pAvt, 0x8d6d, &age, 1 );
    FindAvtFloats( pAvt, 0x3f89, &freckle, 1 );
    if ( bPlayerFace ) {
        // the clinic's age / FRECKLES purchases (surgery_spec.md 1.3)
        age = GetPlayerProfile().FaceAge >= 0.0f ? GetPlayerProfile().FaceAge : age;
        freckle = GetPlayerProfile().FaceFreckle >= 0.0f ? GetPlayerProfile().FaceFreckle : freckle;
    }
    const uint64_t specParmId = GetIdentifier64bit( "SPECPARM" );
    const uint64_t coeffsId = GetIdentifier64bit( "COEFFS" );
    for ( auto &pPart : parts ) {
        for ( EdenMaterial &material : pPart->getMaterials() ) {
            if ( material.ShaderSetGUID != 0xd5013ed0dbcdcdcdull && material.ShaderSetGUID != 0xd5013ed0dbcddbcaull ) {
                continue;
            }
            material.Ambient = Vector4f( ambient[0], ambient[1], ambient[2], 1.0f );
            material.Diffuse = Vector4f( diffuse[0], diffuse[1], diffuse[2], 1.0f );
            material.Specular = Vector4f( specular[0], specular[1], specular[2], 1.0f );
            Vector4f &specParm = material.Parameters[specParmId];
            if ( numSpec >= 2 ) {
                specParm.x = spec[0] * 0.5f;
                specParm.y = spec[1];
                specParm.z = 0.1f; // float @0xede03c (RIMCoeff not used)
            }
            specParm.w = age;
            Vector4f &coeffs = material.Parameters[coeffsId];
            if ( numWrap >= 2 ) {
                coeffs.x = wrap[0];
                coeffs.z = wrap[1];
            }
            coeffs.y = freckle;
        }
    }
    // FUN_00cb1830 (TD_Avatar d5013ed0db79dadb material "EYEBROW"): COLOR =
    // EyebrowsColor (.avt c643, AvatarConfiguration +0x5c u8 RGBA) x
    // 1/255 (float @0xf0c538); render states +0x44 / +0x45 = 2, +0x46 = 1.
    // UVOFF comes from the EyebrowsID atlas table (filled by [ENC] code):
    // the 2DM value is kept.
    float eyebrows[4] = {};
    if ( FindAvtFloats( pAvt, 0xc643, eyebrows, 4 ) >= 3 ) {
        uint8_t bytes[4];
        for ( uint32_t i = 0; i < 4; i++ ) {
            // FUN_00cbd030 stores R, G, B as u8 (+0x5c..+0x5e) through 0x5bbd30
            // [ENC] (INF: x 255, rounded); the alpha is not used by TD_Avatar.
            bytes[i] = static_cast<uint8_t>( Clamp( eyebrows[i] * 255.0f + 0.5f, 0.0f, 255.0f ) );
        }
        const uint64_t colorId = GetIdentifier64bit( "COLOR" );
        const uint64_t eyebrowId = GetIdentifier64bit( "EYEBROW" );
        for ( auto &pPart : parts ) {
            for ( EdenMaterial &material : pPart->getMaterials() ) {
                if ( material.ShaderSetGUID == 0xd5013ed0db79dadbull && material.Hashcode == eyebrowId ) {
                    material.Parameters[colorId] =
                        Vector4f( bytes[0] * 0.0039215f, bytes[1] * 0.0039215f, bytes[2] * 0.0039215f,
                                  bytes[3] * 0.0039215f );
                }
            }
        }
    }
}

void EdenAvatar::setTattoos( const std::vector<TattooDecal> &decals ) {
    tattooDecals = decals;
    if ( pBodiesBank != nullptr ) {
        applyTattoos( *pBodiesBank );
    }
}

void EdenAvatar::applyTattoos( const Bank &bodies ) {
    pBodiesBank = &bodies;
    // -dumpbodyuv <csv>: u v x y z bone material of every vertex of the body mesh (tools/tattoo_uv_probe.py).
    if ( const char *pCsv = CmdLine::GetArgValue( "-dumpbodyuv" ) ) {
        static bool bDumped = false;
        const BankEntry *pGeo = bodies.findEntry( bWoman ? "woman_body" : "man_body", "3dg" );
        Render3DG geometry;
        // the man's mesh unless -dumpbodywoman (the garage loads both; the first one used to win)
        const bool bWantWoman = CmdLine::HasArg( "-dumpbodywoman" );
        if ( !bDumped && bWoman == bWantWoman && pGeo != nullptr && geometry.parse( pGeo->pData, pGeo->Size ) ) {
            bDumped = true;
            FILE *pFile = fopen( pCsv, "w" );
            FILE *pTri = fopen( ( std::string( pCsv ) + ".tri" ).c_str(), "w" );
            uint32_t baseVertex = 0;
            for ( const auto &entry : geometry.getGeometries() ) {
                for ( const GeometryPrimitive &prim : entry.second.Primitives ) {
                    for ( size_t i = 0; pTri != nullptr && i + 2 < prim.Indices.size(); i += 3 ) {
                        fprintf( pTri, "%u,%u,%u\n", baseVertex + prim.Indices[i], baseVertex + prim.Indices[i + 1],
                                 baseVertex + prim.Indices[i + 2] );
                    }
                    baseVertex += static_cast<uint32_t>( prim.Vertices.size() );
                    for ( size_t i = 0; pFile != nullptr && i < prim.Vertices.size(); i++ ) {
                        const MeshVertex &v = prim.Vertices[i];
                        int32_t bone = -1;
                        if ( i < prim.BoneIndices.size() && prim.BoneIndices[i] < prim.BonePalette.size() ) {
                            bone = static_cast<int32_t>( prim.BonePalette[prim.BoneIndices[i]] );
                        }
                        fprintf( pFile, "%.4f,%.4f,%.4f,%.4f,%.4f,%d,%u\n", v.Texcoord[0], v.Texcoord[1], v.Position[0],
                                 v.Position[1], v.Position[2], bone, static_cast<unsigned>( prim.MaterialIndex ) );
                    }
                }
            }
            if ( pFile != nullptr ) {
                fclose( pFile );
            }
            if ( pTri != nullptr ) {
                fclose( pTri );
            }
        }
    }
    if ( gpRender == nullptr || parts.empty() ) {
        return;
    }
    TextureCache &textures = gpRender->getTextureCache();
    tattooRevision++;
    std::unordered_map<uint64_t, GPUTexture *> composited; // base hash -> tattooed copy (shared by the body's materials)
    std::vector<GPUTexture *> previous = std::move( tattooTextures );
    tattooTextures.clear();
    // parts[0] is the body (loadPart order); only its skin materials carry tattoos.
    for ( EdenMaterial &material : parts[0]->getMaterials() ) {
        if ( material.ShaderSetGUID != 0xd5013ed0dbcdcdcdull || material.pProgram == nullptr ) {
            continue;
        }
        // The skin slot: the first colour slot. Remember the untouched texture the first time.
        uint32_t slot = 16;
        for ( uint32_t k = 0; k < material.NumSlots && k < 16; k++ ) {
            if ( material.pTextures[k] != nullptr && !material.bNormalSlot[k] ) {
                slot = k;
                break;
            }
        }
        if ( slot == 16 ) {
            continue;
        }
        SkinSlot *pSaved = nullptr;
        for ( SkinSlot &saved : skinSlots ) {
            if ( saved.pMaterial == &material ) {
                pSaved = &saved;
            }
        }
        if ( pSaved == nullptr ) {
            skinSlots.push_back( { &material, slot, material.pTextures[slot], material.TextureHashes[slot] } );
            pSaved = &skinSlots.back();
        }
        if ( tattooDecals.empty() ) { // no tattoos: the original skin
            material.pTextures[pSaved->Slot] = pSaved->pBase;
            material.TextureHashes[pSaved->Slot] = pSaved->BaseHash;
            continue;
        }
        if ( auto done = composited.find( pSaved->BaseHash ); done != composited.end() ) {
            material.pTextures[pSaved->Slot] = done->second;
            material.TextureHashes[pSaved->Slot] = GetIdentifier64bit( "tattoo_skin" ) + tattooRevision;
            continue;
        }
        // The slot's texture is a .2db of the bodies bank (hash of its name).
        const BankEntry *pEntry = nullptr;
        for ( const BankEntry *pCandidate : bodies.findEntriesByExtension( "2db" ) ) {
            if ( GetIdentifier64bit( pCandidate->Name.c_str() ) == pSaved->BaseHash ) {
                pEntry = pCandidate;
                break;
            }
        }
        Texture2DB texture;
        if ( pEntry == nullptr || !texture.parse( pEntry->pData, pEntry->Size ) || texture.Width == 0 ||
             texture.Format == VF_A8R8G8B8 ) {
            continue;
        }
        std::vector<uint8_t> rgba( static_cast<size_t>( texture.Width ) * texture.Height * 4 );
        DecodeBlockCompressed( texture.pTexels + texture.getMipOffset( 0 ), texture.Width, texture.Height, texture.Format,
                               rgba.data() );
        CompositeTattoos( rgba.data(), texture.Width, texture.Height, tattooDecals );
        if ( const char *pDump = CmdLine::GetArgValue( "-dumptattoo" ) ) {
            SaveImagePng( pDump, rgba.data(), texture.Width, texture.Height );
        }
        const std::string name = "tattoo_" + pEntry->Name;
        if ( GPUTexture *pTexture = textures.createFromRGBA( texture.Width, texture.Height, rgba.data(), name.c_str() ) ) {
            material.pTextures[pSaved->Slot] = pTexture;
            material.TextureHashes[pSaved->Slot] = GetIdentifier64bit( "tattoo_skin" ) + tattooRevision;
            composited[pSaved->BaseHash] = pTexture;
            tattooTextures.push_back( pTexture );
            OTDU_LOG_INFO( "Tattoo: %zu decals composited into %s (%ux%u, slot %u)\n", tattooDecals.size(),
                           pEntry->Name.c_str(), texture.Width, texture.Height, pSaved->Slot );
        }
    }
    for ( GPUTexture *pOld : previous ) {
        textures.destroyTexture( pOld ); // deferred by the device until both frame slots are done
    }
}

// Aim-to-place (docs/re/tattoo_spec.md): the skin atlas is packed and overlapped, so a tattoo is placed by shooting a ray at the
// body and taking the hit triangle's UV. The body mesh is skinned on the CPU with each vertex's first bone (rigid): good enough
// for picking. skinMatrices empty = the bind pose.
bool EdenAvatar::pickSkin( const Vector3f &rayOrigin, const Vector3f &rayDirection, const Matrix4x4f &world,
                           const std::vector<Matrix4x4f> &skinMatrices, float &outU, float &outV ) {
    if ( pBodiesBank == nullptr ) {
        return false;
    }
    if ( !pickMesh.bValid ) {
        const BankEntry *pGeo = pBodiesBank->findEntry( bWoman ? "woman_body" : "man_body", "3dg" );
        Render3DG geometry;
        if ( pGeo == nullptr || !geometry.parse( pGeo->pData, pGeo->Size ) ) {
            return false;
        }
        for ( const auto &entry : geometry.getGeometries() ) {
            for ( const GeometryPrimitive &prim : entry.second.Primitives ) {
                const uint32_t base = static_cast<uint32_t>( pickMesh.Positions.size() );
                for ( size_t i = 0; i < prim.Vertices.size(); i++ ) {
                    const MeshVertex &v = prim.Vertices[i];
                    pickMesh.Positions.push_back( Vector3f( v.Position[0], v.Position[1], v.Position[2] ) );
                    pickMesh.UVs.push_back( { v.Texcoord[0], v.Texcoord[1] } ); // the skin texture row = the mesh V (checked in 3D against 4 anchors)
                    int32_t bone = -1;
                    if ( i < prim.BoneIndices.size() && prim.BoneIndices[i] < prim.BonePalette.size() ) {
                        bone = static_cast<int32_t>( prim.BonePalette[prim.BoneIndices[i]] );
                    }
                    pickMesh.Bones.push_back( bone );
                }
                // Only the skin that is drawn can carry a tattoo: the primitive's material must be a visible TD_Skin2 one.
                bool bSkin = false;
                if ( !parts.empty() && prim.MaterialIndex < parts[0]->getMaterials().size() ) {
                    const EdenMaterial &material = parts[0]->getMaterials()[prim.MaterialIndex];
                    bSkin = material.ShaderSetGUID == 0xd5013ed0dbcdcdcdull && material.pProgram != nullptr;
                    if ( CmdLine::HasArg( "-logpick" ) ) {
                        OTDU_LOG_INFO( "pick prim material %u '%s' guid %llx program %d -> %s\n", static_cast<unsigned>( prim.MaterialIndex ),
                                       material.Name.c_str(), static_cast<unsigned long long>( material.ShaderSetGUID ),
                                       material.pProgram != nullptr ? 1 : 0, bSkin ? "skin" : "ignored" );
                    }
                }
                if ( bSkin ) {
                    pickMesh.TriMaterial.resize( pickMesh.TriMaterial.size() + prim.Indices.size() / 3, prim.MaterialIndex );
                }
                for ( uint32_t index : prim.Indices ) {
                    if ( bSkin ) {
                        pickMesh.Indices.push_back( base + index );
                    }
                }
            }
        }
        pickMesh.bValid = !pickMesh.Indices.empty();
    }
    // Vertices into world space.
    std::vector<Vector3f> world3( pickMesh.Positions.size() );
    for ( size_t i = 0; i < world3.size(); i++ ) {
        Vector3f p = pickMesh.Positions[i];
        const int32_t bone = pickMesh.Bones[i];
        if ( bone >= 0 && static_cast<size_t>( bone ) < skinMatrices.size() ) {
            p = skinMatrices[bone].transformPoint( p );
        }
        world3[i] = world.transformPoint( p );
    }
    float bestT = 1e30f;
    bool bHit = false;
    for ( size_t t = 0; t + 2 < pickMesh.Indices.size(); t += 3 ) {
        const uint32_t i0 = pickMesh.Indices[t], i1 = pickMesh.Indices[t + 1], i2 = pickMesh.Indices[t + 2];
        const Vector3f &a = world3[i0];
        const Vector3f e1 = world3[i1] - a, e2 = world3[i2] - a;
        const Vector3f p = rayDirection.cross( e2 );
        const float det = e1.dot( p );
        if ( std::fabs( det ) < 1e-12f ) {
            continue;
        }
        const float inv = 1.0f / det;
        const Vector3f s = rayOrigin - a;
        const float bu = s.dot( p ) * inv;
        if ( bu < 0.0f || bu > 1.0f ) {
            continue;
        }
        const Vector3f q = s.cross( e1 );
        const float bv = rayDirection.dot( q ) * inv;
        if ( bv < 0.0f || bu + bv > 1.0f ) {
            continue;
        }
        const float tHit = e2.dot( q ) * inv;
        if ( tHit > 0.0f && CmdLine::HasArg( "-logpick" ) ) {
            const float c0 = 1.0f - bu - bv;
            OTDU_LOG_INFO( "  candidate material %u t %.4f uv %.3f %.3f\n", static_cast<unsigned>( pickMesh.TriMaterial[t / 3] ), tHit,
                           c0 * pickMesh.UVs[i0].first + bu * pickMesh.UVs[i1].first + bv * pickMesh.UVs[i2].first,
                           c0 * pickMesh.UVs[i0].second + bu * pickMesh.UVs[i1].second + bv * pickMesh.UVs[i2].second );
        }
        if ( tHit > 0.0f && tHit < bestT ) {
            bestT = tHit;
            bHit = true;
            const float b0 = 1.0f - bu - bv;
            if ( CmdLine::HasArg( "-logpick" ) ) {
                OTDU_LOG_INFO( "pick tri %zu: (%.2f %.2f %.2f) (%.2f %.2f %.2f) (%.2f %.2f %.2f) t %.3f b %.2f %.2f\n", t / 3, a.x, a.y, a.z, world3[i1].x, world3[i1].y, world3[i1].z, world3[i2].x, world3[i2].y, world3[i2].z, tHit, bu, bv );
            }
            outU = b0 * pickMesh.UVs[i0].first + bu * pickMesh.UVs[i1].first + bv * pickMesh.UVs[i2].first;
            outV = b0 * pickMesh.UVs[i0].second + bu * pickMesh.UVs[i1].second + bv * pickMesh.UVs[i2].second;
        }
    }
    return bHit;
}
