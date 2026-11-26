#pragma once

#include "filesystem/bank.h"
#include "render/eden/eden_animation.h"
#include "render/eden/eden_model.h"
#include "game/avatar/tattoo.h"

// Avatars (players and PNJ) assembled like the original (docs/re/avatar_spec.md
// section 3): DBPNJ(_configid) -> .avt (avatar/config/pnj.bnk: BodyID) + the
// clothes of the DBPNJ row (DBClothe._file -> avatar/clothes/<dir>/<file>.bnk,
// colour = material variant of <file>_mat.2dm, hair colour = palette of
// common_clothes hair_mat.2dm), body zones hidden by the worn garments,
// skinned on the man / woman skeleton of common_bodies.bnk.
class EdenAvatarLibrary {
public:
    EdenAvatarLibrary();
    ~EdenAvatarLibrary();

    bool load();
    void unload();
    inline bool isLoaded() const { return bLoaded; }

    const Bank &getBodiesBank() const { return bodiesBank; }
    const EdenSkeleton &getSkeleton( bool bWoman ) const { return bWoman ? womanSkeleton : manSkeleton; }
    const Bank *getClothesBank( const std::string &file );
    const Bank &getConfigBank() const { return configBank; }
    const Render2DM &getHairPalette() const { return hairPalette; }

private:
    Bank bodiesBank;
    Bank clothesCommonBank;
public:
    // The preset .avt's face (the clinic's baseline): the 49 FaceParamArray
    // bytes and the age / freckle floats of a DBPNJ config.
    bool readPresetFace( const char *pConfigId, int8_t outParams[49], float &outAge, float &outFreckle );
private:
    Bank configBank;
    Render2DM hairPalette;
    EdenSkeleton manSkeleton;
    EdenSkeleton womanSkeleton;
    std::unordered_map<std::string, std::unique_ptr<Bank>> clothesBanks;
    bool bLoaded = false;
};

class EdenAvatar {
public:
    EdenAvatar();
    ~EdenAvatar();

    // configId: DBPNJ _configid (e.g. "STO_Party_Woman02").
    bool load( EdenAvatarLibrary &library, const char *pConfigId );
    void unload();
    inline bool isLoaded() const { return pSkeleton != nullptr; }
    inline const EdenSkeleton &getSkeleton() const { return *pSkeleton; }
    inline bool isWoman() const { return bWoman; }
    // .avt FaceParamArray (key 9d93, 49 signed bytes, struct +0xa2; the
    // FaceFX customisation registers, docs/re/facefx_spec.md).
    inline const int8_t *getFaceParams() const { return faceParams; }
    // The clinic's live edit: the 49 FaceParamArray bytes (read each frame by the FaceFX evaluation) and the
    // age / freckle skin values, without a reload.
    // Tattoos (tattoo.h): the decals composited into the body skin; setTattoos recomposites live.
    void setTattoos( const std::vector<TattooDecal> &decals );
    const std::vector<TattooDecal> &getTattoos() const { return tattooDecals; }
    // Aim-to-place: the skin (u, v) under a world-space ray, rigid CPU skinning (skinMatrices empty = bind pose).
    bool pickSkin( const Vector3f &rayOrigin, const Vector3f &rayDirection, const Matrix4x4f &world,
                   const std::vector<Matrix4x4f> &skinMatrices, float &outU, float &outV );
    void setFaceEdit( const int8_t *pParams, float age, float freckle );
    // TD_Skinface facialAnimCoef1 / facialAnimCoef2 (FaceFX normal map
    // weights, FUN_00caf700).
    void setFacialCoefficients( const Vector4f &coef1, const Vector4f &coef2 );

    // skinMatrices: EdenSkeleton::computeSkinMatrices of the current pose.
    void draw( EdenRenderer &renderer, const Matrix4x4f &world, const std::vector<Matrix4x4f> &skinMatrices,
               const struct EdenAvatarLighting *pLighting = nullptr, bool bDrawHead = true ) const;

private:
    const EdenSkeleton *pSkeleton = nullptr;
    bool bWoman = false;
    std::vector<std::unique_ptr<EdenModel>> parts;
    std::vector<size_t> headParts; // head mesh and the clothes worn on it
    int8_t faceParams[49] = {};
    bool bPlayerFace = false; // the player's config: the clinic's face edits apply (PlayerProfile::FaceParams)

    // FUN_00cbe2a0 / FUN_00cbe1a0 / FUN_00cbe220: .avt skin values into the
    // TD_Skin2 / TD_Skinface materials (docs/re/skin_zones_spec.md).
    void applySkin( const struct BankEntry *pAvt );
    // Tattoo compositor (tattoo.h): the body skin texture with the player's decals blended in.
    void applyTattoos( const Bank &bodies );
    std::vector<struct GPUTexture *> tattooTextures; // composited skins this avatar owns
    std::vector<TattooDecal> tattooDecals;
    uint32_t tattooRevision = 0; // the swapped slot hash changes per composite (the draw-set cache keys on it)
    const Bank *pBodiesBank = nullptr;
    struct SkinSlot {
        struct EdenMaterial *pMaterial;
        uint32_t Slot;
        struct GPUTexture *pBase;
        uint64_t BaseHash;
    };
    std::vector<SkinSlot> skinSlots;
    struct PickMesh {
        std::vector<Vector3f> Positions;
        std::vector<std::pair<float, float>> UVs;
        std::vector<int32_t> Bones;
        std::vector<uint32_t> Indices;
        std::vector<uint16_t> TriMaterial; // material index of each triangle
        bool bValid = false;
    } pickMesh;
};
