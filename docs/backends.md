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
| Game boots (services, Flash logic, login) | done | **done** (3D modes refused, section 6) | - | - |
| ImGui overlay / build watermark | done | **done, opt-in** `-d3d12-ui` (driver stall, section 6) | - | - |
| Scene, 2D UI, Flash drawing | done | not yet (phase 3) | - | - |

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
    -userdata E:\Cline\_ud -nopad -exitframe 4000 -autoplay "2000:shot=E:/Cline/_dx.ppm"
```

Measured (RTX 5070 Ti, `build 217`): the whole service list initialises, `GAME MODE : LOGIN`
runs, the frame loop holds **5258 fps** (`[perf] frame 3960: avg 0.2 ms`), the screenshot is a
valid 1280x720 PPM, and the run exits by itself. What is *not* drawn is the Flash login art
(the 2D renderer is Vulkan): the window shows the clear colour. `[probe] bitmap fill ... has
no texture` warnings in the log are expected - `TextureCache` hands out placeholders.

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

1. **D3D12 device + swapchain + self test** - done (2026-11-16).
2. **D3D12 front end** - done (2026-11-16, second pass): the game boots on the backend,
   presents real frames, screenshots, and the ImGui overlay draws (opt-in). `TextureCache`
   and `flash_movie` are null-device safe, the title cinematic is skipped, 3D game modes are
   refused with one log line instead of faulting.
3. **D3D12 scene + 2D/Flash drawing**: the DXSO SPIR-V through SPIRV-Cross + `dxc`, the Eden
   constant buffer path in `EdenRenderer::fillConstants`, `Renderer2D` on D3D12 (that is what
   makes the Flash login art and the game modes visible), then the shadow / reflection / post
   passes.
4. **D3D11**: the same translated HLSL, a thin device; it is the cheap one once (3) exists.
5. **D3D9**: the original bytecode path (a near pass-through), with SM 3.0 limits documented
   where the engine's own shaders cannot follow.

Next step, precisely: `source/render/d3d12/d3d12_device.cpp`, `createCommandQueue` - the SRV
heap exists (section 3.1 uses it for ImGui); add the CBV/SRV/UAV + sampler heaps a *scene*
needs and a `createTexture2D` that mirrors `RenderDevice`'s signature, so `TextureCache` (and
with it `GSFlash` and `Renderer2D`) can be pointed at the D3D12 device.

## 6. Known gaps and traps

- **3D game modes are refused on D3D12.** `switchToNextGameMode` (`tdu_instance.cpp`) allows
  `GM_Login` and `GM_UiViewer` and logs `Renderer d3d12 cannot draw game mode X yet` for the
  rest: their `enter()` builds an EdenWorld through the Vulkan device. The front end (login,
  menus, the Flash *logic*) runs.
- **The title cinematic is skipped** - `GMLogin::loadTitleScene` returns early when
  `!canDrawScene()`, and `TitleScene::unload()` is inert when nothing was loaded (it used to
  talk to a device that does not exist here). `-notitlescene` does the same by hand.
- **`TextureCache` hands out placeholders** when there is no Vulkan device: `createFromRGBA`
  returns one shared `OTDU_NoRenderer` texture, `loadBankTextures` loads nothing. That is why
  the log fills with `[probe] bitmap fill N has no texture` while the Flash display list is
  built - the logic runs, the art does not.
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
- **A D3D12 run that ends within its first second can stall in `GSAudio`'s teardown**
  (reproduced at `-exitframe 130` and `4000`, both < 1 s at 3500-5300 fps; `-exitframe 6000`,
  1.0 s, exits cleanly). It is not the renderer - the frame loop has already stopped and the
  log's last line is the game mode exit. Use a longer `-exitframe` for automated checks.
- **No build stamp on a D3D12 screenshot** unless `-d3d12-ui` is on (the stamp is an ImGui
  overlay). RULES section 6: quote the log banner (`Run: build N (hash) - branch`) instead.
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


