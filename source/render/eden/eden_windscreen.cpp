#include "shared.h"
#include "eden_windscreen.h"

#include "system/cmd_line.h"
#include "render/shaders/builtin/windroplet_vert.h"
#include "render/shaders/builtin/windroplet_frag.h"

namespace {
// Push constants (the device's default layout: vertex + fragment, the same
// block the surface fx uses).
struct DropletPush {
    float Params[4];
};

// The droplet quad vertex (clip space).
struct DropletVertex {
    float X, Y;
    float U, V;
    uint32_t Color; // B8G8R8A8: only A used (the drop's alpha)
};

inline uint32_t AlphaColor( float a ) {
    const uint32_t alpha = static_cast<uint32_t>( Clamp( a, 0.0f, 1.0f ) * 255.0f );
    return alpha << 24;
}
} // namespace

float EdenWindscreen::random( uint32_t &s ) {
    // The same LCG shape the WFX objects seed 0x3039 with (eden_rain).
    s = s * 0x7ff8a3edu + 0x2aa01d31u;
    return static_cast<float>( static_cast<double>( s ) * 2.3283064365386963e-10 );
}

bool EdenWindscreen::initialize( RenderDevice *pDevice ) {
    destroy();
    if ( pDevice == nullptr ) {
        return false;
    }
    this->pDevice = pDevice;
    drops.assign( kMaxDroplets, Drop() );
    OTDU_LOG_INFO( "EdenWindscreen: ready (%u droplets)\n", kMaxDroplets );
    return true;
}

void EdenWindscreen::destroy() {
    if ( pDevice != nullptr && pipeline != VK_NULL_HANDLE ) {
        pDevice->destroyPipeline( pipeline );
    }
    pipeline = VK_NULL_HANDLE;
    pDevice = nullptr;
    drops.clear();
    numAlive = 0;
    nextDrop = 0;
    film = 0.0f;
}

void EdenWindscreen::clear() {
    for ( Drop &drop : drops ) {
        drop.bAlive = false;
    }
    numAlive = 0;
    film = 0.0f;
}

void EdenWindscreen::update( float deltaTime, const WeatherPreset *pWeather ) {
    // The retail cap is 10 live droplets on the per-hour presets (nearly
    // invisible by design). The port's first default scaled it 30x, which
    // turned the screen-space composite into a full-frame streak filter (the
    // owner's "inside cam and bonnet cam have this streak like filter"): 4x
    // keeps a readable shower without fogging the view, -dropletscale 1 is the
    // retail A/B, 8+ a deliberately heavy shower.
    static const float scale = static_cast<float>( Max( 1, CmdLine::GetArgInt( "-dropletscale", 4 ) ) );
    if ( !bEnabled || pWeather == nullptr || deltaTime <= 0.0f ) {
        if ( numAlive != 0 ) {
            clear();
        }
        alpha = 0.0f;
        return;
    }
    // The retail keys (rain_shadow_spec.md A.1: droplets [+0xdbc]: +0x3890 =
    // int(max), +0x3888 = life, +0x3894 = freq, enabled if max > 0).
    const float maxValue = pWeather->getFloat( "rain_droplets_max", 0.0f );
    cap = maxValue * scale;
    lifeTime = Max( 0.25f, pWeather->getFloat( "rain_droplets_life_time", 2.0f ) );
    // The retail spawn freq is tiny (0.15/s on the per-hour presets): with the
    // 0.8 s life the equilibrium would be ~0 visible drops. The rate at least
    // approaches the cap over a life [INF: the retail rate law is not
    // readable]; -dropletscale drives the cap.
    const float retailFreq = pWeather->getFloat( "rain_droplets_spawn_freq", 0.0f ) * scale;
    spawnRate = Max( retailFreq, cap * 0.5f / lifeTime );
    const float refractionKey = pWeather->getFloat( "rain_droplets_refraction_oncam", 15.0f );
    // uv strength [INF: the retail k^2/W,H kernel]; halved from the first port
    // value so the drops bend the scene without swimming the whole frame.
    refraction = refractionKey / 1500.0f;
    alpha = pWeather->getFloat( "rain_alpha", 0.0f );
    // One line per state change (the gate flips with the camera view).
    static float lastLogged = -1.0f;
    if ( alpha != lastLogged ) {
        lastLogged = alpha;
        OTDU_LOG_INFO( "EdenWindscreen: %s (rain_alpha %g, cap %g, freq %g, life %g, film %.2f, drops %u)\n",
                       isActive() ? "on the glass" : "dry", alpha, cap, spawnRate, lifeTime, film, numAlive );
    }
    if ( maxValue <= 0.0f || alpha <= 0.0f ) {
        if ( numAlive != 0 || film > 0.0f ) {
            clear();
        }
        return;
    }

    // The wet film soaks with the rain, the wiper sweep clears it (wipe()).
    // Soak-rate halved: the film is a light mist on the glass, not a screen
    // tint (the streak-filter report).
    film = Min( 1.0f, film + deltaTime * 0.10f * Clamp( alpha / 255.0f, 0.0f, 1.0f ) );

    // Spawn: the retail freq per second, up to the cap.
    if ( numAlive < static_cast<uint32_t>( cap ) ) {
        spawnAccum += spawnRate * deltaTime;
        while ( spawnAccum >= 1.0f && numAlive < static_cast<uint32_t>( cap ) ) {
            spawnAccum -= 1.0f;
            Drop &drop = drops[nextDrop];
            nextDrop = ( nextDrop + 1 ) % kMaxDroplets;
            if ( !drop.bAlive ) {
                numAlive++;
            }
            drop = Drop();
            drop.U = random( seed );
            drop.V = random( seed ) * 0.9f; // the glass gathers drops above the cowl
            drop.LifeTime = lifeTime * ( 0.6f + random( seed ) * 0.8f );
            drop.Life = drop.LifeTime;
            // Skewed sizes: mostly small, the odd large drop (a real shower).
            // Halved from the first port value: a drop covering 8% of the frame
            // height read as a lens over the whole view.
            const float roll = random( seed );
            drop.Size = roll < 0.85f ? ( 0.005f + roll * 0.016f ) : ( 0.018f + roll * 0.024f );
            drop.bAlive = true;
        }
    }

    // Life: the retail alpha law (A.6: life/lifeTime * clamp(2*rain_alpha/255,
    // 0, 1)); a drop sticks for a while, then runs down the glass.
    for ( Drop &drop : drops ) {
        if ( !drop.bAlive ) {
            continue;
        }
        drop.Life -= deltaTime;
        if ( drop.Life <= 0.0f ) {
            drop.bAlive = false;
            numAlive--;
            continue;
        }
        if ( drop.Speed <= 0.0f && drop.Life < drop.LifeTime * 0.6f && random( seed ) < deltaTime * 0.8f ) {
            drop.Speed = 0.05f + random( seed ) * 0.22f; // uv/s [INF]
            drop.Phase = random( seed ) * 6.2831853f;
        }
        if ( drop.Speed > 0.0f ) {
            drop.V += drop.Speed * deltaTime;
            drop.U += sinf( drop.Phase + drop.V * 24.0f ) * drop.Speed * deltaTime * 0.25f;
            if ( drop.V > 1.05f ) {
                drop.bAlive = false;
                numAlive--;
            }
        }
    }
}

void EdenWindscreen::wipe( float x0, float y0, float x1, float y1, float radius ) {
    // Distance of a point to the segment, in uv (y down; the aspect is folded
    // into the caller's radius).
    const float dx = x1 - x0, dy = y1 - y0;
    const float lengthSq = dx * dx + dy * dy;
    for ( Drop &drop : drops ) {
        if ( !drop.bAlive ) {
            continue;
        }
        float t = 0.0f;
        if ( lengthSq > 1e-8f ) {
            t = Clamp( ( ( drop.U - x0 ) * dx + ( drop.V - y0 ) * dy ) / lengthSq, 0.0f, 1.0f );
        }
        const float ex = drop.U - ( x0 + t * dx );
        const float ey = drop.V - ( y0 + t * dy );
        if ( ex * ex + ey * ey <= radius * radius ) {
            drop.bAlive = false;
            numAlive--;
        }
    }
    film = 0.0f; // the blade takes the film with it (it re-soaks in update)
}

void EdenWindscreen::renderDroplets( RenderDevice &device ) {
    static bool bLoggedFirst = false;
    if ( !bLoggedFirst ) {
        bLoggedFirst = true;
        OTDU_LOG_INFO( "EdenWindscreen: renderDroplets first call (drops %u, film %.2f)\n", numAlive, film );
    }
    if ( numAlive == 0 && film <= 0.0f ) {
        return;
    }
    // The pipeline binds the render pass this draw records into (the droplet
    // RT's, cleared on begin) - created on the first use, like the
    // FullscreenPass caches per pass.
    if ( pipeline == VK_NULL_HANDLE ) {
        const VkRenderPass renderPass = device.getCurrentRenderPass();
        if ( renderPass == VK_NULL_HANDLE ) {
            return;
        }
        PipelineDesc desc;
        desc.pVertexSPIRV = kSPIRV_windroplet_vert;
        desc.VertexSPIRVSize = sizeof( kSPIRV_windroplet_vert );
        desc.pFragmentSPIRV = kSPIRV_windroplet_frag;
        desc.FragmentSPIRVSize = sizeof( kSPIRV_windroplet_frag );
        desc.Attributes = { { 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof( DropletVertex, X ) },
                            { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof( DropletVertex, U ) },
                            { 2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof( DropletVertex, Color ) } };
        desc.BindingStrides = { sizeof( DropletVertex ) };
        desc.Blend = eBlendMode::BM_Alpha;
        desc.Cull = eCullMode::CM_None;
        desc.bDepthTest = false;
        desc.bDepthWrite = false;
        desc.RenderPass = renderPass;
        pipeline = device.createPipeline( desc );
        if ( pipeline == VK_NULL_HANDLE ) {
            return;
        }
    }
    VkCommandBuffer commandBuffer = device.getCommandBuffer();
    if ( commandBuffer == VK_NULL_HANDLE ) {
        return;
    }
    // The film first (one flat-normal quad), then the droplets over it.
    if ( film > 0.0f ) {
        DropletPush push = {};
        push.Params[0] = 1.0f; // mode: film
        push.Params[1] = film * 0.16f;
        const DropletVertex quad[6] = {
            { -1.0f, -1.0f, 0.0f, 0.0f, 0 }, { -1.0f, 1.0f, 0.0f, 1.0f, 0 }, { 1.0f, -1.0f, 1.0f, 0.0f, 0 },
            { 1.0f, -1.0f, 1.0f, 0.0f, 0 },  { -1.0f, 1.0f, 0.0f, 1.0f, 0 }, { 1.0f, 1.0f, 1.0f, 1.0f, 0 },
        };
        const TransientAllocation allocation =
            device.allocateTransient( sizeof( quad ), alignof( DropletVertex ) );
        if ( allocation.pData != nullptr ) {
            memcpy( allocation.pData, quad, sizeof( quad ) );
            vkCmdBindPipeline( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
            vkCmdPushConstants( commandBuffer, device.getDefaultPipelineLayout(),
                                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( DropletPush ),
                                &push );
            VkDeviceSize offset = allocation.Offset;
            vkCmdBindVertexBuffers( commandBuffer, 0, 1, &allocation.Buffer, &offset );
            vkCmdDraw( commandBuffer, 6, 1, 0, 0 );
        }
    }
    if ( numAlive == 0 ) {
        return;
    }
    // The per-drop alpha: the retail law life/lifeTime * clamp(2*alpha/255).
    const float lawAlpha = Clamp( 2.0f * alpha / 255.0f, 0.0f, 1.0f );
    std::vector<DropletVertex> vertices;
    vertices.reserve( numAlive * 6 );
    for ( const Drop &drop : drops ) {
        if ( !drop.bAlive ) {
            continue;
        }
        const float lifeFraction = drop.Life / drop.LifeTime;
        // The bigger the lens, the more it bends (the composite refracts by
        // normal x presence): the small drops stay subtle.
        const float a = lawAlpha * Clamp( lifeFraction * 1.6f, 0.0f, 1.0f ) * Clamp( drop.Size * 30.0f, 0.35f, 1.0f );
        // A running drop stretches into a streak; a stuck one is a round lens.
        // The clip-space quad must be narrowed by the target's aspect (x spans
        // the full width, y the full height), or every lens renders wide.
        const float stretch = 1.0f + drop.Speed * 4.0f;
        const float aspect = static_cast<float>( pDropletTarget->Height ) /
                             static_cast<float>( Max<uint32_t>( 1, pDropletTarget->Width ) );
        const float halfW = drop.Size * 0.5f * aspect;
        const float halfH = drop.Size * 0.5f * stretch;
        // uv -> clip (y down: v 0 top).
        const float cx = drop.U * 2.0f - 1.0f;
        const float cy = 1.0f - drop.V * 2.0f;
        const DropletVertex quad[6] = {
            { cx - halfW, cy - halfH, 0.0f, 0.0f, AlphaColor( a ) },
            { cx - halfW, cy + halfH, 0.0f, 1.0f, AlphaColor( a ) },
            { cx + halfW, cy - halfH, 1.0f, 0.0f, AlphaColor( a ) },
            { cx + halfW, cy - halfH, 1.0f, 0.0f, AlphaColor( a ) },
            { cx - halfW, cy + halfH, 0.0f, 1.0f, AlphaColor( a ) },
            { cx + halfW, cy + halfH, 1.0f, 1.0f, AlphaColor( a ) },
        };
        vertices.insert( vertices.end(), quad, quad + 6 );
    }
    if ( vertices.empty() ) {
        return;
    }
    const TransientAllocation allocation =
        device.allocateTransient( vertices.size() * sizeof( DropletVertex ), alignof( DropletVertex ) );
    if ( allocation.pData == nullptr ) {
        return;
    }
    memcpy( allocation.pData, vertices.data(), vertices.size() * sizeof( DropletVertex ) );
    DropletPush push = {};
    push.Params[0] = 0.0f; // mode: droplet
    vkCmdBindPipeline( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
    vkCmdPushConstants( commandBuffer, device.getDefaultPipelineLayout(),
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( DropletPush ), &push );
    VkDeviceSize offset = allocation.Offset;
    vkCmdBindVertexBuffers( commandBuffer, 0, 1, &allocation.Buffer, &offset );
    vkCmdDraw( commandBuffer, static_cast<uint32_t>( vertices.size() ), 1, 0, 0 );
}
