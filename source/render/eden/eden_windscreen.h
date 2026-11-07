#pragma once

#include "render/vulkan/render_device.h"
#include "render/eden/eden_weather.h"

#include <vector>

// Rain on the windscreen (the retail droplets-on-camera effect, docs/re/
// rain_shadow_spec.md A.6: FUN_00aa2100 TD_Droplets_Oncam, the RTs
// Droplets_OnCam / Droplets_FBCopy / Droplets_Windshield, weather keys
// rain_droplets_max / _life_time / _spawn_freq / _refraction_oncam). The
// simulation keeps the live droplets (screen space, uv), renders them into a
// small offscreen RT (normal.xy + presence) the final blit refracts the scene
// with (windscreen.frag), and carries the wet film (a flat-normal term the
// wiper sweep clears). The retail droplet cap is tiny (the per-hour presets
// ship rain_droplets_max 10, the cinematic ones 1) - -dropletscale multiplies
// the cap and the spawn rate for a denser look [INF: the retail scale is not
// readable].
class EdenWindscreen {
public:
    bool initialize( RenderDevice *pDevice );
    void destroy();

    // The camera owner's gate (the inside views; the effect is screen space,
    // it shows whenever the camera sits behind glass). Off: nothing spawns,
    // nothing draws.
    inline void setEnabled( bool bEnable ) { bEnabled = bEnable; }
    inline bool isEnabled() const { return bEnabled; }
    // Raining (the frame's weather) and enabled: the composite runs.
    inline bool isActive() const { return bEnabled && alpha > 0.0f; }
    // The strength of the wet film 0..1 (the composite fogs by it).
    inline float filmStrength() const { return film; }
    inline float refractionStrength() const { return refraction; }
    inline uint32_t numDroplets() const { return numAlive; }

    void setTarget( RenderTarget *pTarget ) { pDropletTarget = pTarget; }

    // Simulation (once per frame, the resolved rain weather).
    void update( float deltaTime, const WeatherPreset *pWeather );
    // The droplet RT draw (inside the target's begin/endRenderTarget).
    void renderDroplets( RenderDevice &device );
    // The wiper sweep: a screen-space segment (uv, y down) cleared this frame.
    // Drops near it die, the film resets (it re-soaks over the next seconds).
    void wipe( float x0, float y0, float x1, float y1, float radius );

    void clear();

private:
    struct Drop {
        float U = 0.0f;      // screen uv (0..1, y down)
        float V = 0.0f;
        float Speed = 0.0f;  // downward, uv per second (0: stuck in the film)
        float Size = 0.0f;   // uv height of the lens
        float Phase = 0.0f;  // the wobble of the running drop
        float Life = 0.0f;
        float LifeTime = 1.0f;
        bool bAlive = false;
    };

    RenderDevice *pDevice = nullptr;
    // The droplet pipeline binds the render pass of the droplet RT, which
    // only exists inside renderDroplets (lazily created there, like the
    // FullscreenPass caches per pass).
    VkPipeline pipeline = VK_NULL_HANDLE;
    RenderTarget *pDropletTarget = nullptr;

    std::vector<Drop> drops;
    uint32_t numAlive = 0;
    uint32_t nextDrop = 0;

    float alpha = 0.0f;     // rain_alpha of the frame (0..255)
    float film = 0.0f;      // the wet film 0..1
    float refraction = 0.02f;
    float spawnAccum = 0.0f;
    float cap = 0.0f;       // live droplets (retail max * -dropletscale)
    float spawnRate = 0.0f; // per second (retail freq * -dropletscale)
    float lifeTime = 2.0f;
    bool bEnabled = false;

    static constexpr uint32_t kMaxDroplets = 2048;

    float random( uint32_t &seed );
    uint32_t seed = 0x571234ab;
};
