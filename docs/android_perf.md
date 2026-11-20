# Android performance — what the phone does per frame, and what to do about it

The test device is an **Honor Magic 8 Pro**: Snapdragon 8 Elite Gen 5
(SM8850-AC, 2x Oryon V3 Phoenix L @ 4.6 GHz + 6x Oryon V3 Phoenix M @ 3.62 GHz,
armv9.2), **Adreno 840**, 12 GB RAM, 1256x2808 LTPO panel at 120 Hz, Android 16.
That is the fastest Android SoC of 2026, so when a 2011 PC game struggles on it
the cost is in the port's habits, not in the hardware.

Read this with `docs/backends.md` (the backend matrix) and the `[perf]` lines of
`docs/state/main.md`.

---

## 1. What a frame costs (the instruments that already exist)

| Instrument | What it prints |
|---|---|
| `-showfps` | the overlay: FPS, `update / offscreen / main / ui / present / wait`, the scene draw-call count |
| `-loggputimes` | `[perf] gpu: env … ocean … prepass … shadows … planar … clouds … opaque … clouddepth … blended … post … main …` — one GPU timestamp pair per pass (`RenderDevice::beginGpuPass`) |
| `[perf]` (always, every 120 frames) | `avg ms (fps)`, `update avg/max`, `draw avg/max`, `draw split: offscreen / main / ui / end(present)`, `vram used / budget` |
| `[hitch]` (> 100 ms, `-hitchms`) | the frame, its CPU sections and the texture uploads of that frame (`N tex, X MB, N submits, N waits`) |

A phone run therefore reports itself: the Android launcher passes
`-showfps -loggputimes` when the FPS switch is on, and the log lines above are
the whole measurement. **The first thing to look at on a slow frame is where the
milliseconds are**: a large `end(present)` is waiting for the display (pacing,
not the engine), a large `offscreen` is the scene pass, a large `update` is the
simulation / streaming.

## 2. The levers that exist today

### Quality profile (launcher: Phone / Low / Medium / High / Custom)

The launcher's **Phone (fastest)** preset is
`-profile low -renderscale 50 -drawdistance 50 -fpslimit 30 -aniso 1 -noclouds
-cubemapstagger -cubemapinterval 2 -norearfaces`. Every one of those is a
`CmdLine` flag, so they can be tried one at a time from a desktop build too.

### Texture filtering (`aniso`) — the biggest sampler cost on a phone

`GameOptions::TextureQuality` (the retail Video page row) maps 1 → trilinear,
2 → 4x, 3 → 16x anisotropic; `ApplyVideoOptions` turns it into
`RenderDevice::setMaxAnisotropy` + `EdenRenderer::refreshSamplers` (live).
Before 2026-11-20 no quality profile touched it, so a phone ran **16x
anisotropic filtering on every texture** — a driving game is the worst case for
it: the road and the terrain fill the screen at a grazing angle, and 16x aniso is
16 taps per sample plus the cache thrash that comes with it.

* Profiles now set it: High 16x, Medium / Deck / Legion / 60 fps 4x, **Low 1x**,
  and the Phone preset passes `-aniso 1` explicitly (so it also wins over an
  `options.ini` that still says 16x).
* `-aniso <1|2|4|8|16>` overrides the row for a run; the Advanced settings page
  grew a **Texture filtering** row that applies immediately, so it can be
  flipped on the phone while watching the FPS overlay.
* Measured on the desktop build (FREERIDE spawn, 1280x720, `-aniso 1` against the
  16x default): mean absolute pixel difference **0.75 / 0.62 / 0.42** per
  channel — invisible at that camera distance, and it is the setting with the
  largest known GPU cost on a mobile part.

### `-texcap <px>` — cap the texture size

`TextureCache::createFrom2DB` drops the mip levels above `<px>` before decoding
and uploading (`firstMip`), so a 2048 texture loads as 1024. Survey of 500 banks
(islands / scenarics / vehicules / interior / frontend, 2961 textures, script
`_texstat3.py`):

| width | 64 | 128 | 256 | 512 | 1024 | 2048 |
|---|---|---|---|---|---|---|
| textures | 517 | 723 | 842 | 281 | 568 | 5 |

The art tops out at 1024 with a handful of 2048s, so **`-texcap 1024` costs
nothing visible** (and saves little); `-texcap 512` is the real
memory/bandwidth knob: on the FREERIDE spawn it took VRAM from **1695 MB to
1567 MB** (-7.5 %) at a mean pixel difference of 0.35 / 0.25 / 0.16 — but the
ground textures are where the 1024s live, so check a road / grass close-up
before making it a default. It is **not** in the Phone preset for that reason.

### The frame rate limiter

`source/tdu_instance.cpp` now sleeps with `SDL_DelayPrecise` for the whole
remaining frame time. The old loop spun on a big core for the last 2 ms (battery
and thermal budget for nothing) and used `SDL_Delay(1)`, which on Android
oversleeps by several ms — visible as an uneven 30 fps cadence on a 120 Hz panel.

## 3. The structural problem: no block-compressed textures

Adreno does not expose BC1/BC3 (`supportsBC` is false), so
`TextureCache::createFrom2DB` **decodes every DXT texture to RGBA8** on the phone
(`DecodeBlockCompressed`, `source/render/2db.cpp`). The survey above measures the
consequence:

**528.8 MB of stored BC → 3834.8 MB of RGBA8 (7.3x).**

That is 7.3x the VRAM *and* 7.3x the sampling bandwidth for the same art, plus a
CPU decode on the main thread while the world streams (the `[hitch]` line names
those frames: `uploads 1773 tex 94.0 MB`). Two directions, in order of value:

1. **Transcode to ASTC 4x4 (or ETC2) at load** — same 4x4 block, 1 byte per texel
   like DXT1, so memory and bandwidth stay where the retail art had them and the
   quality is close to BC3. Needs an encoder: `astcenc` is portable C++ but slow
   (1-5 Mpx/s/core, so it needs an on-disk cache) or a **BC→ASTC proxy encoder**
   (map the BC endpoints onto ASTC weight grids — a few kernels, seconds for a
   whole island, slightly below `astcenc` quality). Cache the result under
   `<userdata>/texcache/<hash>.astc` so the first load pays and later ones do not.
2. **A faster decoder** meanwhile. `DecodeBlockCompressed` was rewritten
   (2026-11-20) to a table-driven, direct-write decoder: **bit-identical output**
   (checked block for block against the old implementation over DXT1/3/5 at 14
   size classes, harness `_bctest.cpp`) and 3.4 ms → 2.6 ms per 1024² DXT1+DXT5
   pair on one desktop core (2.3 → 3.1 GB/s decoded). The next step is a real
   NEON kernel (`vqtbl1q_u8` over the 4-colour palette, 4 pixels per
   instruction) — which is where this port's ARM work mirrors RPCS3's: their
   arm64 acceleration came from rewriting the hot kernels to use NEON / dotprod
   directly, not from a compiler flag.


## 4. ARM build settings (`CMakeLists.txt`, `if( ANDROID )`)

* `-march=armv8.2-a+dotprod+fp16` (default): the NDK baseline is armv8-a from
  2014; every arm64 phone that can run the port (minSdk 29) is armv8.2 with the
  dot product and fp16. `-DOTDU_ANDROID_BASELINE_ARMV8A=ON` reverts,
  `-DOTDU_ANDROID_ARM_V9=ON` builds `armv9.2-a+i8mm` for this phone only.
* `-flto=thin` on the engine and the link (`-DOTDU_ANDROID_LTO=OFF` disables):
  the .so went 13.49 MB → 13.06 MB and the JNI entry points (`JNI_OnLoad`, every
  `Java_org_libsdl_app_*`) are still exported — checked with `llvm-nm -D` on the
  .so inside the APK.
* `-ffp-contract=fast -fno-math-errno`.

## 5. What to try next, in order

1. **Measure first**: run the phone build with `-showfps -loggputimes`, drive for
   a minute, and read `[perf] gpu:` plus `draw split`. Everything below depends on
   which of `opaque` / `prepass` / `clouds` / `post` / `end(present)` carries the
   frame.
2. **ASTC/ETC2 transcoding** (§3) — the one change that removes the 7.3x.
3. **Tile-friendliness of the render passes**: `vkCmdClearAttachments`
   (`RenderDevice::clearRenderTargetDepth`) and any mid-pass
   `vkCmdPipelineBarrier` force a GMEM round trip on Adreno, and a target's depth
   attachment keeps `STORE_OP_STORE` even when nothing samples it. A/B with the
   existing `-nopassbarrier`, then make the store ops conditional.
4. **Secondary command buffers**: `EdenRenderer::flushPendingRecords` splits
   batches of ≥192 draws across threads (`-noparallelrecord` turns it off). The
   per-secondary-CB cost is higher on Adreno than on a desktop driver — worth an
   A/B on the phone before assuming the parallel path wins.
5. **CPU streaming**: texture decode + upload run on the main thread; move the
   decode (and the transcode) onto the pool `gs_file` already has for prefetching,
   and spread the uploads over frames (the `[hitch]` lines show 94 MB in one
   frame).
6. **Swapchain**: the scene renders at `-renderscale` and is upscaled into the
   full-res swapchain. Rendering the swapchain at the logical size and letting the
   display compositor scale would remove a full-res pass and the present-sized
   writes; the HUD would have to stay at full resolution (its own viewport) to
   keep the text crisp.

