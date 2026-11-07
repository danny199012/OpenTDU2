#pragma once

#include "render/eden/eden_renderer.h"

struct WeatherPreset;
struct RenderTarget;

// TD_Ocean sea of the original renderer (docs/re/ocean_spec.md):
// - noise normal map: NoiseSlice 32^3 volume -> NOISE_COMPOSITE 32^2
//   (td_noisecompose, 3 animated slices) -> NOISE_ASSEMBLE_F 256^2
//   (td_noiseassembly, 4 octaves) -> NoiseNMap 256^2 (TD_ocean_nmap_from_hmap);
// - reflection: SEAREF_N, the scene from the mirror camera (no sky, no water,
//   no blended draws) in Ocean_RTRefl 256^2 cleared to ocean_reflectionColor,
//   clipped at the water plane;
// - the TD_Ocean materials get their textures and constants every frame
//   (FUN_00961710 / FUN_00982f60).
class EdenOcean {
public:
    static constexpr uint64_t kOceanShaderSet = 0x0cea000000000002ull;
    enum eKind : uint8_t { OK_Sector = 0, OK_Area = 1, OK_Island = 2 };

    using DrawReflected = std::function<void( EdenRenderer &renderer, const EdenSceneParams &mirrorScene )>;

public:
    EdenOcean();
    ~EdenOcean();

    bool initialize( EdenRenderer &renderer );
    void destroy();
    inline bool isReady() const { return bReady; }

    // Noise normal map passes (outside render passes).
    void updateNormalMap( EdenRenderer &renderer, float deltaTime, const WeatherPreset *pWeather );
    // Reflection render (outside render passes): scene = main camera.
    void renderReflection( EdenRenderer &renderer, const EdenSceneParams &scene, const WeatherPreset *pWeather,
                           const DrawReflected &drawReflected );
    // Runtime setup of a TD_Ocean material (FUN_008be1e0 + per frame
    // constants).
    void setupMaterial( EdenRenderer &renderer, EdenMaterial &material, eKind kind, GPUTexture *pColorMap,
                        const WeatherPreset *pWeather ) const;

    // Port enhancements (not retail; see docs/re/ocean_spec.md 11):
    // - wave drift: the retail shader only morphs the noise volume (one slice
    //   per second at a fixed world UV), which reads as a still surface. The
    //   vertex shader feeds ShiftWorld (the floating origin, 0 in retail) into
    //   the same wave UV, so the port puts a wind drift there: the swell
    //   visibly travels. -nowavedrift turns it off, -wavespeed <m/s> and
    //   -wavedir <deg> tune it.
    // - water clarity: the water blends with what is behind it (the sea bed,
    //   and later the sea life) by the GameOptions water_clarity amount.
    //   -waterclear 0 restores the opaque retail water.
    // - shore swell (docs/re/ocean_spec.md 11.4): the whole sea surface rises
    //   and falls by GameOptions water_tide centimetres (0..20, default 12), so
    //   the waterline washes up and down the beaches. EdenWorld applies
    //   getWaterLevel() to the TD_Ocean items; the reflection plane follows.
    void setWaveDrift( float speed, float directionDegrees ) {
        waveSpeed = speed;
        waveAngle = directionDegrees * kPI / 180.0f;
    }
    inline Vector4f getWaveShift() const { return waveShift; }
    inline bool isWaveDriftEnabled() const { return waveSpeed > 0.0f; }
    // The tide offset of this frame in metres (0 = the island's water plane).
    inline float getWaterLevel() const { return waterLevel; }
    // The island's own water plane height (mesh y): 0 on Ibiza / Casino,
    // about -1.8 on Hawai (docs/re/ocean_spec.md 5.3). The tide oscillates
    // around this base; the reflection plane and the logged sea level use
    // base + level, the loaded ocean meshes only need the tide offset.
    inline void setWaterBaseLevel( float base ) { waterBaseLevel = base; }
    inline float getWaterBaseLevel() const { return waterBaseLevel; }
    inline float getWaterPlaneHeight() const { return waterBaseLevel + waterLevel; }

    // Debug: the SEAREF_N reflection buffer (256^2, mirror camera), null
    // before the first reflection pass. -showsearefl blits it.
    inline GPUTexture *getReflectionTexture() const {
        return pReflection != nullptr && pReflection->bInitialized ? &pReflection->Color : nullptr;
    }
    // Debug: the NoiseNMap wave normal map (256^2, regenerated every frame
    // from the 32^3 NoiseSlice volume). -showseanmap blits it.
    inline GPUTexture *getNormalMapTexture() const {
        return pNormalMap != nullptr && pNormalMap->bInitialized ? &pNormalMap->Color : nullptr;
    }

private:
    GPUTexture *pNoiseSlice = nullptr;
    RenderTarget *pComposite = nullptr;
    RenderTarget *pAssemble = nullptr;
    RenderTarget *pNormalMap = nullptr;
    RenderTarget *pReflection = nullptr;
    GPUBuffer *pVertexBuffer = nullptr;
    GPUBuffer *pIndexBuffer = nullptr;
    EdenMesh quad;
    EdenMaterial composeMaterial;
    EdenMaterial assembleMaterial;
    EdenMaterial normalMapMaterial;
    Matrix4x4f reflectionMatrix; // REFLMAT rows (column-vector convention)
    float phase = 0.0f;
    // Port wave drift (ShiftWorld): the world distance the wave normal map has
    // slid, wrapped to a noise tile so the float stays small.
    float waveSpeed = 1.5f;     // m/s along waveAngle
    float waveAngle = 0.6f;     // radians, direction of travel
    float waveDriftTime = 0.0f; // seconds, monotonic
    Vector4f waveShift = Vector4f( 0.0f, 0.0f, 0.0f, 0.0f );
    // Port shore swell: the water plane height of this frame (m) and its
    // clock (one 9.2 s surf cycle).
    float waterLevel = 0.0f;
    // The island water mesh height (0 Ibiza / Casino, -1.8 Hawai) the tide
    // and the reflection plane are relative to.
    float waterBaseLevel = 0.0f;
    float tideTime = 0.0f;
    bool bReady = false;

    void pass( EdenRenderer &renderer, RenderTarget *pTarget, EdenMaterial &material, const EdenDrawParams &params );
};
