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
| Depth-stencil (Flash masks) | done | **done** | - | - |
| Textures from `.2db` / RGBA | done | **done** | - | - |
| Game boots (services, Flash logic, login) | done | **done** | - | - |
| 2D layer (Flash front end, font atlas, clip rects, stencil masks) | done | **done** (HLSL, section 6) | - | - |
| ImGui overlay / build watermark | done | **done, opt-in** `-d3d12-ui` (driver stall, section 6) | - | - |
| Scene (eden), 3D game modes | done | not yet (phase 3b) | - | - |

`eRenderBackend` (`source/render/rhi/render_backend.h`) is the switch,
`GSRender::createDevice` is the single place that acts on it, and
`GSRender::canDrawScene()` / `canDraw2D()` are what the frame loop asks before
drawing anything [CODE]. On D3D12 the scene is still skipped (phase 3b below),
and the 2D layer - the Flash front end, its bitmaps, the font atlases, the clip
rectangles and the stencil masks - is drawn by the D3D12 device itself.

## 2. Picking a backend

| Flag / variable | Meaning |
|---|---|
| `-renderer vulkan` | the complete renderer, the default |
| `-renderer d3d12` / `-renderer=d3d12` | the D3D12 device and swapchain (aliases `dx12`, `direct3d12`) |
| `-renderer d3d11` / `d3d9` | recognised, not implemented yet - a warning, then Vulkan |
| `OTDU_RENDERER=d3d12` | the same, when no flag is given |
| `-d3d12-debug` | D3D12 debug layer + DRED (names the operation a GPU fault was running) |
| `-d3d12-warp` | force WARP, the software adapter (CI / no-GPU machines) |
| `-d3d12-no2d` | no 2D layer at all: the frame stays the clear colour (the A/B for the 2D path) |
| `-d3d12-nostencil` | draw the 2D batches with the stencil test off (the A/B for a mask issue) |
| `-d3d12-logui` | one log line per frame with the 2D batch and vertex counts, plus the first batch |
| `-d3d12-uitest` | draw three test quads (flat colour, a BC3 game texture, a gradient) over the frame |
| `-d3d12-ui` | draw the ImGui overlay (build watermark, debug panels) on D3D12 - opt-in, section 6 |
| `-noimgui` | no ImGui overlay at all, on either backend (the A/B for a UI-side stall) |
| `-notitlescene` | skip the 3D press-start cinematic (applied automatically when the backend cannot draw 3D) |
| `-renderer-selftest [frames]` | the self test below: no game, no data, exit 0/1 |
| `-renderer-shot <path.ppm>` | with the self test: write the last frame |
| `-renderer-window` | with the self test: show the window instead of hiding it |

A name that is unknown, or a backend that was not compiled in
(`OTDU_ENABLE_D3D12` / `_D3D11` / `_D3D9`, only meaningful under `WIN32`), logs a
warning and falls back to Vulkan - a typo must never stop the game from starting
[CODE].

## 3. Verifying it

### 3.1 The game itself on D3D12

```powershell
build-cline\bin\opentdu2-cline.exe -renderer d3d12 -game C:\TDU2-Gamedata\TestDrive2 `
    -userdata E:\Cline\_ud-dx2d -nopad -exitframe 9000 -autoplay "8000:shot=E:/Cline/_dx.ppm"
```

Measured (RTX 5070 Ti, `build 218 (…) - cline`): the whole service list initialises,
`GAME MODE : LOGIN` runs, the **Flash front end draws** (`Renderer d3d12: 2D layer ready
(HLSL via D3DCompile, 4 stencil modes, 4194304 px upload ring per frame)`), the frame loop
holds **~2600 fps** (`[perf] frame 1080: avg 0.4 ms`), the screenshot is a valid 1280x720
PPM, and the run exits by itself.

**The frame is compared with Vulkan, not looked at.** The 3D title cinematic is skipped on
D3D12 (`canDrawScene()`), so the reference is a Vulkan run with `-notitlescene` at the same
*movie* time - and D3D12 renders so fast that `-exitframe`/`-autoplay` frame numbers are a
different movie moment (0.5 s at frame 1200 vs 2.5 s at Vulkan's frame 300). Two runs at
the same moment:

```powershell
# Vulkan, Flash only (the 3D title cinematic off)
... -renderer vulkan -notitlescene ... -exitframe 320 -autoplay "300:shot=E:/Cline/_vk.ppm"
# D3D12 (≈2.5 s of movie time at ~2600 fps)
... -renderer d3d12 ...              -exitframe 9000 -autoplay "8000:shot=E:/Cline/_dx.ppm"
```

Measured: Vulkan `non-black 27.96 %, 17095 distinct colours`; D3D12 `non-black 27.55 %,
16997 distinct colours`; pixel difference between the two frames **mean |Δ| 0.60/255, 99.26 %
of pixels within 8 levels** (Pillow, `ImageChops.difference`). That is the front end - the
LOG_IN2 movie's dim intro, its gradient bands, its bitmaps and its font atlases - drawn by
the D3D12 pipeline.

Two smaller checks, both headless:

- `-d3d12-uitest` draws three quads over the frame (flat colour through the white texture, a
  BC3 `.2db` texture, a gradient texture). The BC3 quad reads back **2034 distinct colours**
  (blues like `60,102,154`) - proof that the mip upload and the SRV are right, which a
  uniformly black texture (a bad copy) would not give.
- `-d3d12-logui` prints `D3D12 UI: frame N: 22 batches (20 colour), 972 vertices`, so a
  frame that loses its batches is distinguishable from one that draws them wrongly.

`[probe] bitmap fill ... has no texture` warnings are still expected for the few shapes whose
page genuinely does not resolve - the same lines appear on Vulkan.

With the overlay on, on the software rasterizer (the driver stall below does not happen
there):

```powershell
build-cline\bin\opentdu2-cline.exe -renderer d3d12 -d3d12-ui -d3d12-warp -game ... `
    -exitframe 4000 -autoplay "2000:shot=E:/Cline/_dx_ui.ppm"
```

The screenshot carries the build watermark: sampled every 7th pixel it has **57 distinct
colours and 735 non-black samples** - i.e. text, not an empty clear.

### 3.2 The backend alone (no game data)

Before `TestDriveGameInstance` exists (`source/tdu_instance.cpp`, `TestDrive::InitAndRun`) [CODE]:


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
and compare them with the expected colours. Measured on build `215 (947c66b) - cline`:
`white 255,255,255`, `yellow 255,255,0`,
`cyan 0,255,255`, `green 0,255,0`, `magenta 255,0,255`, `red 255,0,0`,
`blue 0,0,255`, `black 0,0,0` - eight of eight, at bar centres x = 80..1200 on
row 360 of a 1280x720 frame. `-autoplay "20:shot=..."` inside the game would use
the same readback, but an in-game D3D12 run is refused until phase 2 (section 6)
[CODE].

## 4. The shader path

The scene cannot be ported until the shaders can be consumed per backend. Both
inputs already exist in the tree:

1. **Original Eden shaders**: D3D9 bytecode in the `.sob` libraries [DATA].
2. **Engine shaders**: GLSL built to SPIR-V at build time, embedded as
   `uint32_t` arrays (`source/render/shaders/builtin/*.h`) [CODE].

| Backend | Path | Notes |
|---|---|---|
| D3D12 | SPIR-V -> HLSL (SPIRV-Cross) -> DXIL (`dxc`) | one translation, reused by D3D11 with SM 5.0 targeting |
| D3D11 | SPIR-V -> HLSL -> SM 5.0 (`dxc` or `D3DCompile`) | same output as D3D12 |
| D3D9 | the **original bytecode directly** (`vs_3_0` / `ps_3_0`) | no translation at all for Eden's shaders; the engine's own GLSL would have to be hand-ported to SM 3.0 (lossy: no MRT, no integer ops) |

The 2D layer did not need that pipeline: it is **one HLSL pair compiled at run
time by `D3DCompile`** (`d3dcompiler_47.dll`, which every Windows has) to SM 5.0
**DXBC**, which D3D12 accepts directly [CODE]. `dxc` / `dxcompiler.dll` are not
part of the Windows SDK install on this machine (`C:\Program Files (x86)\Windows
Kits\10\bin\10.0.26100.0\x64` has no `dxc.exe`), so the SPIR-V -> DXIL route
stays the plan for the *scene* (it needs SM 6 features and the `.sob` shaders),
not for the 2D pipeline.

Then, per backend: bindings (root signature / CBV+SRV+sampler heaps vs
descriptor sets), the push-constant block (`kPushConstantsSize` = 128 B) becomes
root constants or a CBV, and the pipeline state (blend / cull / depth / stencil in
`PipelineDesc`) maps to `D3D12_GRAPHICS_PIPELINE_STATE_DESC`.

## 5. Order of work

1. **D3D12 device + swapchain + self test** - done (2026-11-16).
2. **D3D12 front end** - done (2026-11-16, second pass): the game boots on the backend,
   presents real frames, screenshots, and the ImGui overlay draws (opt-in).
3. **D3D12 2D layer** - done (2026-11-17): depth-stencil, `.2db`/RGBA texture upload,
   `D3D12UI` (root signature, 4 PSOs, upload ring, HLSL via `D3DCompile`), `Renderer2D`'s
   D3D12 branch. The Flash front end draws - 99.26 % of its pixels match the Vulkan
   `-notitlescene` frame within 8 levels (section 3.1).
4. **D3D12 scene (phase 3b)**: the DXSO SPIR-V through SPIRV-Cross + `dxc`, the Eden
   constant buffer path in `EdenRenderer::fillConstants`, the render targets / shadow /
   reflection / post passes, and then the 3D game modes (still refused with one log line).
5. **D3D11**: the same translated HLSL, a thin device; it is the cheap one once (4) exists.
6. **D3D9**: the original bytecode path (a near pass-through), with SM 3.0 limits documented
   where the engine's own shaders cannot follow.

Next step, precisely: `source/render/eden/eden_shader_system.cpp` - the `.sob` pipeline
creation (SPIR-V -> HLSL -> DXIL) and `EdenRenderer::fillConstants`' push constants as a
root CBV, so `EdenWorld`/`EdenSceneView` can be pointed at a D3D12 device the way
`TextureCache` and `Renderer2D` already are.

## 6. Known gaps and traps

- **3D game modes are refused on D3D12.** `switchToNextGameMode` (`tdu_instance.cpp`) allows
  `GM_Login` and `GM_UiViewer` and logs `Renderer d3d12 cannot draw game mode X yet` for the
  rest: their `enter()` builds an EdenWorld through the Vulkan device. The front end (login,
  menus, the Flash *art* as well as its logic) runs.
- **The title cinematic is skipped** - `GMLogin::loadTitleScene` returns early when
  `!canDrawScene()`, and `TitleScene::unload()` is inert when nothing was loaded (it used to
  talk to a device that does not exist here). `-notitlescene` does the same by hand, which is
  also the right A/B frame: with the 3D cinematic off, Vulkan and D3D12 render the same Flash
  layer and can be compared pixel by pixel (section 3.1).
- **Direct3D's clip space is +Y up, Vulkan's is +Y down.** `Renderer2D::flush` negates the
  projection's Y row (`projection[5]` and `[13]`, the only non-zero entries in row 1 of the
  column-major matrix) for D3D12. Without it the whole 2D layer draws mirrored, and negating
  the wrong entries puts it off screen entirely - the two traps cost this session an hour.
- **The stencil buffer is real now** (`D24_UNORM_S8_UINT`, cleared per frame, four PSOs: none /
  INCR_SAT / DECR_SAT / equal). The Flash masks' increment-decrement scheme needs it; the
  mask batches write no colour (`RenderTargetWriteMask = 0`), like the Vulkan pipelines.
- **`TextureCache` uploads the .2db formats natively on D3D12**: BC1 / BC2 / BC3, BC5 for
  ATI2, `B8G8R8A8` for D3D9's A8R8G8B8, `R8G8B8A8`, everything else through the existing
  CPU conversion. Cube maps are **not** uploaded (Eden only), and `A8`/`L8` deliberately keep
  the Vulkan conversion to RGBA8 - an `A8_UNORM` view would put the font atlas' coverage in
  alpha, while the shader reads `texel.r`.
- **The texture upload blocks** (`ExecuteCommandLists` + `waitIdle`) because it happens at
  load time; the SRV comes from the device's shader-visible heap (`kSrvDescriptors` = 8192,
  shared with ImGui). `-d3d12-logui` prints the first eight uploads with their format and
  first bytes when a texture looks wrong.
- **D3D12 renders at ~2600 fps here, Vulkan at ~120 fps**, so `-exitframe` / `-autoplay` frame
  numbers are *not* comparable across backends: the Flash movie is time-driven, so 1200
  D3D12 frames are 0.5 s of movie while Vulkan's 300 are 2.5 s. A black D3D12 screenshot at a
  low frame number is the movie's black intro, not a broken layer.
- **The ImGui overlay is opt-in on D3D12 (`-d3d12-ui`) because it stalls on the NVIDIA
  driver here.** Evidence, in order: the backend is initialised and its device objects
  (root signature, PSO, font atlas) are created successfully; frame 0 has *no* geometry
  (`0 lists, 0 vtx`) and completes including Present; frame 1 has `1 list, 196 vtx` and stops
  inside `ImGui_ImplDX12_RenderDrawData` with an idle CPU while the log mutex stays held (so
  the watchdog cannot report either). There is no host wait on that path - the only
  `WaitForSingleObject` in the backend is the font upload, which had already completed. The
  **same build on WARP draws the overlay and exits cleanly** (1258 fps, watermark in the
  screenshot), so the command stream is valid; it is the hardware driver that does not
  return. `-noimgui` is the same A/B on the Vulkan side.
- **A D3D12 run can stall in the process teardown** (after the last frame; the frame loop and
  every frame are fine). It is flaky, not deterministic: the same command sometimes exits and
  sometimes stops with an idle CPU, and it reproduces with `-noaudio` and with `-nolog`. There
  is a second flavour of the same family: a *terminated* run can leave a zombie
  `opentdu2-cline` process object behind (0 handles, no path) whose exe lock makes the next
  `ninja` link fail with `failed to write output ... permission denied` - wait a few seconds
  and rebuild, it is not a build error. Automation: let the run be stopped
  (`Stop-Process -Name opentdu2-cline`) or rely on the watchdog's kill threshold.
  Next step: `TestDriveGameInstance::terminate` - log with `Logger::WriteTagged`
  (non-blocking) from each step to find the holder.
- **No build stamp on a D3D12 screenshot** unless `-d3d12-ui` is on (the stamp is an ImGui
  overlay). RULES section 6: quote the log banner (`Run: build N (hash) - branch`) instead.
- **`Options > Video` still configures the Vulkan object** in a D3D12 run
  (`ApplyVideoOptions` calls `RenderDevice::setVSync` / `setMaxAnisotropy`,
  `source/game/game_options.cpp`); those setters only write fields, so nothing
  crashes, but the D3D12 swapchain ignores them for now.
- **Only VSync off is exercised** by the self test; the game path passes the
  video setting through. Tearing is used only when VSync is off *and* the adapter
  reports `DXGI_FEATURE_PRESENT_ALLOW_TEARING`.
- **No MSAA** in the D3D12 device yet, and the depth buffer carries no scene depth (the 2D
  layer never tests it); both belong to phase 3b.
- The device reports a loss once (`reportDeviceLost`) and the game loop leaves on
  it, matching `RenderDevice::hasDeviceLost` [CODE].


