# Direct3D backends (12, then 11, then 9)

Owner's request (2026-11-16): *"work on Direct X 9, 11 & 12 support start with
12 work down"*. This file is that plan and its status board.

## 1. What the seam is now

The port is Vulkan-only and the renderer is **not abstracted**: `RenderDevice`
(`source/render/vulkan/render_device.*`) exposes Vulkan handles, and every
renderer above it - `EdenRenderer` (it holds `VkDescriptorPool` members),
`Renderer2D`, `TextureCache`, `RayTracing`, `EdenSurfaceFx` - calls them directly
[CODE]. TDU2's own shaders are D3D9 bytecode, translated to SPIR-V by DXVK's DXSO
(`source/render/shaders/dxso_translator.*`), so the engine shader path already
speaks "D3D9 in, SPIR-V out" [CODE].

Rewriting all of that behind an RHI in one pass would be a multi-month change
with no shippable step in between. The backends are therefore added
**around** the Vulkan path, newest first:

| Piece | Vulkan | D3D12 | D3D11 | D3D9 |
|---|---|---|---|---|
| Device / swapchain / Present | done | **done** | not started | not started |
| Clear + viewport + scissor | done | done | - | - |
| Screenshot (PPM readback) | done | **done** | - | - |
| Resize, minimise, device lost | done | done | - | - |
| Scene, 2D UI, Flash, ImGui | done | not yet (phase 2) | - | - |

`eRenderBackend` (`source/render/rhi/render_backend.h`) is the switch,
`GSRender::createDevice` is the single place that acts on it, and
`GSRender::canDrawScene()` is what the frame loop asks before drawing anything
[CODE]. A Direct3D run therefore presents the swapchain and nothing else; every
draw path above is skipped instead of being run against an uninitialised Vulkan
device.

## 2. Picking a backend

| Flag / variable | Meaning |
|---|---|
| `-renderer vulkan` | the complete renderer, the default |
| `-renderer d3d12` / `-renderer=d3d12` | the D3D12 device and swapchain (aliases `dx12`, `direct3d12`) |
| `-renderer d3d11` / `d3d9` | recognised, not implemented yet - a warning, then Vulkan |
| `OTDU_RENDERER=d3d12` | the same, when no flag is given |
| `-d3d12-debug` | D3D12 debug layer + DRED (names the operation a GPU fault was running) |
| `-d3d12-warp` | force WARP, the software adapter (CI / no-GPU machines) |
| `-renderer-selftest [frames]` | the self test below: no game, no data, exit 0/1 |
| `-renderer-shot <path.ppm>` | with the self test: write the last frame |
| `-renderer-window` | with the self test: show the window instead of hiding it |

A name that is unknown, or a backend that was not compiled in
(`OTDU_ENABLE_D3D12` / `_D3D11` / `_D3D9`, only meaningful under `WIN32`), logs a
warning and falls back to Vulkan - a typo must never stop the game from starting
[CODE].

## 3. Verifying it

The self test runs **before** `TestDriveGameInstance` exists, so it needs no game
data (`source/tdu_instance.cpp`, `TestDrive::InitAndRun`) [CODE]:

```powershell
build-cline\bin\opentdu2-cline.exe -renderer d3d12 -renderer-selftest 60 `
    -renderer-shot E:\Cline\_dx12.ppm
```

It creates a hidden window, the device and the flip model swapchain, presents 60
frames of an eight-bar colour test pattern (white, yellow, cyan, green, magenta,
red, blue, black - cleared with `ClearRenderTargetView` rectangles; a flat clear
could be a stale buffer, bars cannot) and writes the last frame as binary PPM.
Exit code 0 = passed. The lines to look for:

```
D3D12: adapter '<card>' (<n> MB)
D3D12: device '<card>', 1280x720, 3 buffers, vsync off, tearing available
D3D12 self test: 60 frame(s) presented on '<card>' (1280x720)
Screenshot saved to 'E:\Cline\_dx12.ppm'
-renderer-selftest: PASSED
```

The result can be checked without eyes: sample the eight bar centres of the PPM
and compare them with the expected colours. Measured on the RTX 5070 Ti build
`214 (c879c91+dirty) - cline`: `white 255,255,255`, `yellow 255,255,0`,
`cyan 0,255,255`, `green 0,255,0`, `magenta 255,0,255`, `red 255,0,0`,
`blue 0,0,255`, `black 0,0,0` - eight of eight, at bar centres x = 80..1200 on
row 360 of a 1280x720 frame. `-autoplay "20:shot=..."` inside the game would use
the same readback, but an in-game D3D12 run is refused until phase 2 (section 6)
[CODE].

## 4. The shader path (phase 2, the real work)

The scene cannot be ported until the shaders can be consumed per backend. Both
inputs already exist in the tree:

1. **Original Eden shaders**: D3D9 bytecode in the `.sob` libraries [DATA].
2. **Engine shaders**: GLSL built to SPIR-V at build time, embedded as
   `uint32_t` arrays (`source/render/shaders/builtin/*.h`) [CODE].

| Backend | Path | Notes |
|---|---|---|
| D3D12 | SPIR-V -> HLSL (SPIRV-Cross) -> DXIL (`dxc`, in the Windows SDK) | one translation, reused by D3D11 with SM 5.0 targeting |
| D3D11 | SPIR-V -> HLSL -> SM 5.0 (`dxc` or `D3DCompile`) | same output as D3D12 |
| D3D9 | the **original bytecode directly** (`vs_3_0` / `ps_3_0`) | no translation at all for Eden's shaders; the engine's own GLSL would have to be hand-ported to SM 3.0 (lossy: no MRT, no integer ops) |

D3D12 is the natural first backend for the scene: `dxc.exe` and
`dxcompiler.dll` ship with the installed Windows SDK (10.0.26100) [CODE-env], so
no new dependency is needed, and the DXIL is reusable for D3D11.

Then, per backend: bindings (root signature / CBV+SRV+sampler heaps vs
descriptor sets), the push-constant block (`kPushConstantsSize` = 128 B) becomes
root constants or a CBV, and the pipeline state (blend / cull / depth / stencil in
`PipelineDesc`) maps to `D3D12_GRAPHICS_PIPELINE_STATE_DESC`.

## 5. Order of work

1. **D3D12 device + swapchain + self test** - done, this session.
2. **D3D12 textures, buffers, root signature, `dxc` hook, ImGui** - the
   smallest visible step: the debug overlay and the 2D UI on D3D12.
3. **D3D12 scene**: the DXSO SPIR-V through SPIRV-Cross + `dxc`, the Eden
   constant buffer path in `EdenRenderer::fillConstants`, then the shadow /
   reflection / post passes.
4. **D3D11**: the same translated HLSL, a thin device; it is the cheap one once
   (3) exists.
5. **D3D9**: the original bytecode path (a near pass-through), with SM 3.0
   limits documented where the engine's own shaders cannot follow.

Next step, precisely: `source/render/d3d12/d3d12_device.cpp`,
`createCommandQueue` - add the descriptor heaps (CBV/SRV/UAV + sampler) and a
root signature, then a `createTexture2D` that mirrors `RenderDevice`'s signature
so `TextureCache` can be pointed at it.

## 6. Known gaps and traps

- **`-renderer d3d12` without `-renderer-selftest` refuses to start the game**
  (`source/tdu_instance.cpp`, `TestDrive::InitAndRun`, exit code 2). The services
  upload their textures through the Vulkan device while they initialise
  (`GSFlash` -> `TextureCache::createFromRGBA` -> `RenderDevice::createTexture2D`),
  so a full run faulted inside `GSFlash::initialize`; the refusal replaced a
  half-started crash with one precise log line. Lifting it needs the phase 2
  port, and `TextureCache` / `flash_movie.cpp` made null-device safe on the way.
- **No build stamp on a D3D12 screenshot.** RULES section 6 asks every test
  screenshot to carry `BUILD N HASH BRANCH`; that stamp is drawn by the 2D UI
  (Vulkan), so a `-renderer d3d12` screenshot has none until step 2. Quote the
  log banner (`Run: build N (hash) - branch`) with the file instead.
- **`Options > Video` still configures the Vulkan object** in a D3D12 run
  (`ApplyVideoOptions` calls `RenderDevice::setVSync` / `setMaxAnisotropy`,
  `source/game/game_options.cpp`); those setters only write fields, so nothing
  crashes, but the D3D12 swapchain ignores them for now.
- **Only VSync off is exercised** by the self test; the game path passes the
  video setting through. Tearing is used only when VSync is off *and* the adapter
  reports `DXGI_FEATURE_PRESENT_ALLOW_TEARING`.
- **No depth buffer, no MSAA** in the D3D12 device yet: a clear-only frame needs
  neither, and guessing the formats before the scene port would only be replaced.
- The device reports a loss once (`reportDeviceLost`) and the game loop leaves on
  it, matching `RenderDevice::hasDeviceLost` [CODE].


