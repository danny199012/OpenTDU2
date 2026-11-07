#pragma once

#include "render/eden/eden_model.h"
#include "render/texture_cache.h"

#include <algorithm>
#include <cmath>
#include <vector>

// The roof light bar of a police car (the traffic models and the chase's cop
// car share it): the `Gyro` lens material plus the FA_G_<A|B|G|R><1..4> glow
// quads of the model (docs/re/car_lights_spec.md section 6).
//
// [DATA] the lens material is TD_CarPaint with DETAIL `gyro_of` (bar off),
// GLOSSLIGHT `gyro_g` (128x16 DXT5 mask) and TEXLIGHT `gyro_on` (128x16
// DXT1), and it carries a GLOSSFCT parameter like the player lamps. The data
// ships GLOSSFCT = (1,1,1,1), i.e. drawn as loaded every ring of both
// reflectors is lit at once - the "roof lights not accurate" look.
//
// The mask's channels are four concentric rings around the two rotating
// reflectors of the bar: r the centre, g the first ring, b the second, a the
// outer one (the white patches of `_police_gyro_g_<channel>.png`). The FA_G
// groups sit at the same radii - FA_G_R nearest the reflector centres,
// FA_G_G, FA_G_B, FA_G_A outermost - and all use the additive FRL_G glow
// material.
//
// The bar driver is a sweep: one ring at a time from the centre outward, then
// the ring repeats. GLOSSFCT carries the four ring intensities in that order
// (x = R, y = G, z = B, w = A) and the quads of the unlit rings are hidden
// per draw through EdenDrawParams::ForceHiddenNodes (a shared model drawn per
// agent). The retail driver is FUN_00c5a9a0: four lamps with staggered phase
// timers modulo a period [CODE]; the period itself is not in the readable
// data, so 1.6 s per turn is [INF].
class PoliceLightBar {
public:
    static constexpr int32_t kPhases = 4; // R (centre) .. A (outer ring)
    // The sweep period: four rings per turn [INF, see the header comment].
    static constexpr float kCycleSeconds = 1.6f;
    // A ring is drawn while its intensity is above this (the fade in / out of
    // the pulse would otherwise pop).
    static constexpr float kVisibleLevel = 0.15f;
    // The GLOSSFCT channel of each ring: R -> x, G -> y, B -> z, A -> w.
    static int32_t PhaseOfNode( const char *pName ) {
        // FA_G_<letter><index> (the letter is at index 5).
        switch ( pName[5] ) {
        case 'A': return 3;
        case 'B': return 2;
        case 'G': return 1;
        case 'R': return 0;
        default: return -1;
        }
    }

    // The pulse of every ring for a clock in seconds. Each ring is lit for a
    // quarter of the turn with a raised cosine fade; adjacent rings blend.
    static void intensities( float seconds, float out[kPhases] ) {
        const float u = seconds / kCycleSeconds;
        for ( int32_t i = 0; i < kPhases; i++ ) {
            float d = u - static_cast<float>( i ) / static_cast<float>( kPhases );
            d -= std::floor( d + 0.5f ); // [-0.5, 0.5)
            const float distance = std::fabs( d );
            constexpr float kHalfWidth = 0.24f; // in turns: a 0.48-turn pulse
            out[i] = distance < kHalfWidth ? 0.5f * ( 1.0f + std::cos( 3.14159265f * distance / kHalfWidth ) ) : 0.0f;
        }
    }

    // Gathers the Gyro materials and the FA_G quads of a loaded model. Safe on
    // any model: a car without a bar leaves this object empty. pTextures
    // resolves the two lens arts (DETAIL gyro_of = off, TEXLIGHT gyro_on =
    // on) so the bar can be switched without the sprite quads.
    void collect( EdenModel &model, TextureCache *pTextures ) {
        Materials.clear();
        for ( int32_t phase = 0; phase < kPhases; phase++ ) {
            Nodes[phase].clear();
        }
        pOffTexture = nullptr;
        pOnTexture = nullptr;
        static const uint64_t kGlossFct = GetIdentifier64bit( "GLOSSFCT" );
        static const uint64_t kDetail = GetIdentifier64bit( "DETAIL" );
        static const uint64_t kTexLight = GetIdentifier64bit( "TEXLIGHT" );
        std::vector<EdenMaterial> &materials = model.getMaterials();
        for ( uint32_t i = 0; i < materials.size(); i++ ) {
            const std::string &name = materials[i].Name;
            const bool bGyro = name.size() >= 4 &&
                               ( name.compare( 0, 4, "Gyro" ) == 0 || name.compare( 0, 4, "GYRO" ) == 0 );
            if ( !bGyro || materials[i].Parameters.count( kGlossFct ) == 0 ) {
                continue;
            }
            Materials.push_back( i );
            if ( pTextures == nullptr ) {
                continue;
            }
            uint64_t offHash = 0, onHash = 0;
            for ( uint32_t slot = 0; slot < materials[i].NumSlots; slot++ ) {
                if ( materials[i].SlotTypes[slot] == kDetail ) {
                    offHash = materials[i].TextureHashes[slot];
                } else if ( materials[i].SlotTypes[slot] == kTexLight ) {
                    onHash = materials[i].TextureHashes[slot];
                }
            }
            pOffTexture = offHash != 0 ? pTextures->find( offHash ) : nullptr;
            pOnTexture = onHash != 0 ? pTextures->find( onHash ) : nullptr;
        }
        for ( const EdenModel::Item &item : model.getItems() ) {
            const SceneNode &node = model.getScene().getNodes()[static_cast<size_t>( item.NodeIndex )];
            if ( node.Name.compare( 0, 5, "FA_G_" ) != 0 || node.Name.size() < 7 ) {
                continue;
            }
            const int32_t phase = PhaseOfNode( node.Name.c_str() );
            if ( phase < 0 ) {
                continue;
            }
            std::vector<int32_t> &group = Nodes[phase];
            if ( std::find( group.begin(), group.end(), item.NodeIndex ) == group.end() ) {
                group.push_back( item.NodeIndex );
            }
        }
    }

    inline bool isPoliceBar() const {
        return !Materials.empty() || !Nodes[0].empty() || !Nodes[1].empty() || !Nodes[2].empty() ||
               !Nodes[3].empty();
    }
    inline uint32_t getMaterialCount() const { return static_cast<uint32_t>( Materials.size() ); }
    inline const std::vector<int32_t> &getNodes( int32_t phase ) const { return Nodes[phase & 3]; }

    // The bar off: the lens shows its dark gyro_of art.
    void applyOff( EdenModel &model ) const { writeState( model, 0.0f, 0.0f, 0.0f, 0.0f ); }

    // The lens art: on binds the lit gyro_on texture, off the dark gyro_of
    // one. The sprite quads of the retail model are billboards the port
    // cannot place accurately (they float off the roof), so the lens art plus
    // the GLOSSFCT sweep is what the bar shows.
    void applyLens( EdenRenderer &renderer, EdenModel &model, bool bOn ) const {
        if ( pOffTexture == nullptr || pOnTexture == nullptr ) {
            return;
        }
        GPUTexture *pTexture = bOn ? pOnTexture : pOffTexture;
        std::vector<EdenMaterial> &materials = model.getMaterials();
        for ( const uint32_t index : Materials ) {
            renderer.assignTexture( materials[index], "DETAIL", pTexture );
        }
    }
    inline bool hasLensArt() const { return pOnTexture != nullptr && pOffTexture != nullptr; }

    // The bar on for the model's clock (seconds): GLOSSFCT = the ring pulses.
    void apply( EdenModel &model, float seconds ) const {
        float v[kPhases];
        intensities( seconds, v );
        writeState( model, v[0], v[1], v[2], v[3] );
    }

    // Adds the quads of the rings that are not lit to the per-draw hide list.
    void hideInactive( EdenDrawParams &params, float seconds ) const {
        float v[kPhases];
        intensities( seconds, v );
        for ( int32_t phase = 0; phase < kPhases; phase++ ) {
            if ( v[phase] < kVisibleLevel ) {
                params.ForceHiddenNodes.insert( params.ForceHiddenNodes.end(), Nodes[phase].begin(),
                                                Nodes[phase].end() );
            }
        }
    }
    // Every quad (the bar is off, or the car is not a police car).
    void hideAll( EdenDrawParams &params ) const {
        for ( int32_t phase = 0; phase < kPhases; phase++ ) {
            params.ForceHiddenNodes.insert( params.ForceHiddenNodes.end(), Nodes[phase].begin(), Nodes[phase].end() );
        }
    }

private:
    void writeState( EdenModel &model, float r, float g, float b, float a ) const {
        static const uint64_t kGlossFct = GetIdentifier64bit( "GLOSSFCT" );
        std::vector<EdenMaterial> &materials = model.getMaterials();
        for ( const uint32_t index : Materials ) {
            auto it = materials[index].Parameters.find( kGlossFct );
            if ( it != materials[index].Parameters.end() ) {
                it->second = Vector4f( r, g, b, a );
            }
        }
    }

    std::vector<uint32_t> Materials;      // indices of the Gyro lens materials
    std::vector<int32_t> Nodes[kPhases]; // FA_G quads per ring
    GPUTexture *pOffTexture = nullptr;   // DETAIL gyro_of (bar off)
    GPUTexture *pOnTexture = nullptr;    // TEXLIGHT gyro_on (bar on)
};
