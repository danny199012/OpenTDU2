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
| Scene shader path (SPIR-V -> HLSL -> SM 5.1 DXBC) | n/a (SPIR-V) | **done** (section 4) | - | - |
| Scene pipelines / meshes / texture tables | done | **done** (one root signature, section 4) | - | - |
| Every Eden shader stage compiled (`-d3d12-shadertest`) | n/a | **done** (section 3.5) | - | - |
| ImGui overlay / build watermark | done | **done, opt-in** `-d3d12-ui` (driver stall, section 6) | - | - |
| Scene (eden), 3D game modes | done | not yet (phase 3b step 2) | - | - |

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
| `-d3d12-scenetest` | with the self test: the scene path instead of the colour bars (section 3.3) |
| `-d3d12-shadertest [<text>]` | in the game bootstrap (`-game`): every `.sob` stage through the scene shader path, with a tally; `<text>` keeps only libraries whose name contains it (section 3.5) |
| `-d3d12-dump-shaders <dir>` | write the HLSL of every pipeline the scene builds (one `.hlsl` per stage) |
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

### 3.3 The scene path (no game data)

`-d3d12-scenetest` swaps the colour bars for the scene pipeline: the engine's own
`resources/shaders/mesh.vert` + `mesh.frag` (the pair the Vulkan renderer draws
`.3dg` geometry with, embedded as SPIR-V in `source/render/shaders/builtin/`) go
through the full path - SPIR-V -> HLSL (SPIRV-Cross) -> SM 5.0 DXBC
(`D3DCompile`) -> `D3D12Scene`'s root signature and pipeline cache - and draw a
textured, lit, depth tested cube (24 vertices, 36 indices) with the checker
texture generated in the test itself.

```powershell
build-cline\bin\opentdu2-cline.exe -renderer d3d12 -d3d12-scenetest `
    -renderer-selftest 30 -renderer-shot E:\Cline\_dx_scenetest.ppm -d3d12-debug
```

Measured (RTX 5070 Ti, build 219): the debug layer reports **no** message for the
whole stream, and the log reads

```
D3D12 scene: root signature ready (b0 constants, b13 root constants, t0..t7)
D3D12 scene test: mesh.vert -> 1720 B DXBC (32 root constants), mesh.frag -> 2564 B DXBC (1 texture(s), 0 sampler(s))
D3D12 scene: pipeline 1 (4 attributes, 1 stream(s), blend 0, cull 0, depth test+write)
D3D12 scene test: 30 frame(s) of a 36 index cube on 'NVIDIA GeForce RTX 5070 Ti' (1280x720)
```

The frame is checked by numbers, not by eye: **10.26 % of the pixels differ from
the clear colour, 28992 distinct colours**, and the three visible faces carry
different brightness (the `+Y` face lit, the two sides dimmer) - i.e. the vertex
layout, the 128-byte root constant block, the texture table with its static
sampler and the depth test all work. `docs/images/d3d12_scene_test.webp` is that
frame. The 28992 colours are the 64x64 checker through linear filtering plus the
per-pixel lighting, not noise: the mean difference between neighbouring pixels
inside the cube is 6.2/765.

The same frame is **byte-identical** (`SHA256 E505B37F...778CEB`) before and after
the SM 5.0 -> 5.1 switch and the push constants' move to register space 1 (section
4) - the compiler changed, the pixels did not. Reproduce the numbers with:

```python
from PIL import Image
im = Image.open(r'E:\Cline\_dx_scenetest.ppm').convert('RGB'); px = list(im.getdata()); bg = px[0]
print('distinct', len(set(px)), 'diff %.2f%%' % (100.0 * sum(1 for p in px if p != bg) / len(px)), bg)
```

`-d3d12-dump-shaders <dir>` writes the HLSL next to the run; reading
`pipeline1_vs.hlsl` is how the first "black frame" was diagnosed as a
`SizeInBytes` mistake rather than a shader problem (section 6).

### 3.4 What is still not drawn

The scene path above is the *mechanism*, not the game: `EdenRenderer` still talks
to the Vulkan device, so `canDrawScene()` is false on D3D12 and the 3D modes stay
refused. Wiring Eden onto `D3D12Scene` is phase 3b step 2 (section 5).

### 3.5 The shader tally (`-d3d12-shadertest`)

The measurement that sizes phase 3b step 2 - *can the Direct3D 12 path carry
Eden's shaders at all?* - is the game bootstrap without the game. It needs the
game data (`Euro/Shaders`) and nothing else: no window, no device, no frame.

```powershell
build-cline\bin\opentdu2-cline.exe -renderer d3d12 -d3d12-shadertest `
    -game C:\TDU2-Gamedata\TestDrive2 -userdata E:\Cline\_ud-dx -log E:\Cline\_dx_shadertest.log
```

Every vertex and pixel stage of every `.sob` library goes through DXSO -> SPIR-V
-> HLSL (SPIRV-Cross) -> SM 5.1 DXBC (`D3DCompile`), one log line per library, a
tally, the limits, and **exit 0 only when every stage compiled** (1 otherwise;
`-d3d12-shadertest <text>` keeps only libraries whose name contains `<text>`, so
one family can be iterated on). `-verbose` adds a line per stage (push constant
size/register, cbuffer count, textures, samplers, DXBC size) and the cbuffer
remap; `-d3d12-dump-shaders <dir>` writes the HLSL of every stage, failures
included. The pass takes minutes: it beats the watchdog (like `-scantunnels`), so
the default 60 s kill threshold is not a problem.

Measured (RTX 5070 Ti is not involved - this is `D3DCompile` on the CPU; build
`221 (242afdd+dirty) - cline`, 2026-11-17):

```
D3D12 shadertest: TD_Building                  11376 permutation(s), 1328 stage(s), 0 failed
D3D12 shadertest: TD_Gouraud                    3968 permutation(s),  596 stage(s), 0 failed
...
D3D12 shadertest: 116 libraries, 31470 permutation(s), 7744 stage(s): 7744 compiled, 0 translate failure(s), 0 HLSL/D3DCompile failure(s) in 488.5 s
D3D12 shadertest: limits - 10 texture(s) max (0 stage(s) over the 16-entry root signature table), 96 B push constants max (24 root constant(s))
D3D12 shadertest: PASSED (7744 of 7744 stages translated)
```

`in 482.3 s ... 13 stage(s) over the 8-entry root signature table` was the
identical run before the table grew to 16 (below). A library's stage count is its
VS attribs plus its PS attribs and its permutation count is its `Item` table -
31470 permutations across 7744 compiled stages, i.e. most permutations share a
stage.

**Every one of the 7744 stages compiles** - and it took three runs and three real
fixes to get there. The tally's value was that each failure was a *silent* defect
in the step-1 code:

| Run | Compiled | What stopped the rest |
|---|---|---|
| SM 5.0 (step 1's setting) | 2221 | 5523 x SPIRV-Cross *"Shader model 5.1 or higher is required to use bindless resources or NonUniformResourceIndex"* |
| SM 5.1, no extra flag (stopped early) | 0 of the first ~1500 | every stage X3596: *"unbounded size descriptor tables are disabled ... use /enable_unbounded_descriptor_tables"* |
| SM 5.1 + unbounded tables + space 1 | **7744** | - (488.5 s; the 99 s of the first run was short because 5523 stages failed before `D3DCompile` was reached) |

The third failure was the quiet one and the reason this section exists: with SM
5.1 and the flag, `TD_Building_ps_343` failed with X4610 (*cbuffer binding ranges
overlap*) while `TD_Accessories2_ps_0` compiled. Dumping the HLSL of both showed
the root constant block at `b0` in the first and `b13` in the second - 1146 of the
first 1470 dumped stages were wrong. The cause is in SPIRV-Cross's remap: its key
is the *original* (set, binding), DXSO numbers images by D3D9 sampler slot, and a
shader that samples `s13` therefore owns the key the push constant block uses. The
fix is register space 1 (section 4) - and a shader that compiled with the root
constants at b0 would have drawn with garbage constants in step 2, with no error
at all.

What the tally sizes for the rest of the Eden work:

* **10 textures** is the maximum any stage asks for. The table was 8 entries
  (`D3D12Scene::kTextureTableSize`) when the tally first measured it, and **13
  stages wanted more** (that run's limits line); it is 16 now - the compiler's
  per-stage ceiling (`D3D12ShaderCompiler::kMaxResources`) and enough for the
  measured maximum - and the re-run prints `0 stage(s) over`.
* **96 B of push constants** (24 root constants) is the maximum, well inside the
  128 B / 32-dword budget the Vulkan path uses.
* ...and the sampler is the open item: DXSO declares it as a *runtime descriptor
  array*, which D3D12's static samplers cannot serve (section 4).

## 4. The shader path

The scene cannot be ported until the shaders can be consumed per backend. Both
inputs already exist in the tree:

1. **Original Eden shaders**: D3D9 bytecode in the `.sob` libraries [DATA].
2. **Engine shaders**: GLSL built to SPIR-V at build time, embedded as
   `uint32_t` arrays (`source/render/shaders/builtin/*.h`) [CODE].

| Backend | Path | Notes |
|---|---|---|
| D3D12 | SPIR-V -> HLSL (SPIRV-Cross) -> **SM 5.1 DXBC** (`D3DCompile`) | done, `D3D12ShaderCompiler`; DXIL / `dxc` not needed (nothing a D3D9 shader expresses needs SM 6) |
| D3D11 | SPIR-V -> HLSL -> SM 5.1 (`dxc` or `D3DCompile`) | same output as D3D12 |
| D3D9 | the **original bytecode directly** (`vs_3_0` / `ps_3_0`) | no translation at all for Eden's shaders; the engine's own GLSL would have to be hand-ported to SM 3.0 (lossy: no MRT, no integer ops) |

The 2D layer did not need that pipeline: it is **one HLSL pair compiled at run
time by `D3DCompile`** (`d3dcompiler_47.dll`, which every Windows has) to SM 5.0
**DXBC**, which D3D12 accepts directly [CODE].

**The scene uses the same route, and it works** (`D3D12ShaderCompiler`,
`source/render/d3d12/d3d12_shader.*`):

1. SPIR-V -> HLSL through **SPIR-V Cross**, vendored in `third_party/spirv_cross`
   (Apache-2.0, pinned to the `vulkan-sdk-1.4.350.0` tag - the same release the
   tree's Vulkan-Headers / SPIRV-Headers / volk come from, so it builds offline
   like the dxvk subset does). `options.shader_model = 51`: **5.1, not 5.0**,
   because DXSO emits `NonUniformResourceIndex` (D3D9 selects its sampler with a
   register operand) and SPIRV-Cross refuses that below 5.1. Plus
   `point_size_compat` / `point_coord_compat`, which is what the D3D9 point
   sprites in `.sob` shaders need (HLSL has neither builtin).
2. HLSL -> **SM 5.1 DXBC** through `D3DCompile` with
   `D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES` - the SM 5.1 HLSL declares the
   dynamically indexed sampler table unbounded, and FXC rejects that by default
   (error X3596). `d3dcompiler_47` writes 5.1 DXBC, D3D12 accepts 5.1 DXBC (its
   floor is feature level 11_0 / SM 5.1), so `dxc` / DXIL is **not** on the
   critical path. (`dxc.exe` does exist on this machine -
   `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe` and
   `C:\VulkanSDK\1.4.350.0\Bin\dxc.exe` - it is simply not needed.)

The register convention is fixed, so **one root signature serves every program**
(`D3D12Scene::createRootSignature`):

| | | |
|---|---|---|
| b0 (VS) / b0 (PS) | CBV | the stage's constant buffers, remapped to b0..bN per stage |
| b13 **space1** | 32-bit root constants | the D3D9 push constants, up to 32 dwords (128 B, `RenderDevice::kPushConstantsSize`) |
| t0..t15 | SRV table | the material's textures, remapped in SPIR-V resource order |
| s0..s15 | static samplers | linear / wrap / mip linear (D3D9's default state) |

* **b13, not b15**: SM 5.0 has exactly 14 constant buffer slots (b0..b13) and
  `D3DCompile` rejects `register(b15)` with *"maximum cbuffer exceeded. target has
  14 slots"*. The push constant block therefore lands on the last slot.
* **...and in register space 1, not space 0.** SPIRV-Cross keys its resource
  remaps by the *original* (set, binding), and DXSO numbers its images by D3D9
  sampler slot - so a shader that samples through `s13` has a resource at (set 0,
  binding 13), the exact key of the push constant block. The lookup then takes
  the *image's* (unset) cbv remap and moves the root constants to b0, where they
  collide with the D3D9 constant file (FXC error X4610) or, worse, compile with
  garbage constants. The tally caught it: **1146 of the first 1470 dumped stages
  had b0 instead of b13**. Register space 1 is free for the purpose and costs
  nothing - a D3D12 root parameter carries its own space.
* **Vertex inputs are `TEXCOORD<location>`**: SPIRV-Cross writes that semantic for
  every SPIR-V location, so `D3D12Scene`'s input layout uses the same rule and a
  `.sob` shader's `dcl` slot needs no lookup table.
* The HLSL text is kept per stage: `-d3d12-dump-shaders <dir>` writes it out, and
  a failed stage's HLSL is dumped too (section 3.5).

**Still open for step 2: the sampler heap.** DXSO declares its samplers as one
*runtime descriptor array* (`SamplerState sampler_heap[] : register(s0, space0)`,
`_SetupSamplerArray` in `dxso_compiler.cpp`) indexed by a value the shader reads
out of the push constant block (`render_state_s0_s1_idx` ...). D3D12 cannot index
*static* samplers dynamically, so the scene's eight static samplers can carry the
engine's own GLSL shaders and the 2D layer but not an Eden draw: the port needs
the Vulkan renderer's **sampler heap** equivalent - a shader-visible sampler
descriptor heap, filled per material (`EdenRenderer`'s `samplerHeap` on Vulkan),
bound as a sampler descriptor table, with the root signature's table at
`s0`, space 0.

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
4. **D3D12 scene (phase 3b)** - step 1 done (2026-11-17): `D3D12ShaderCompiler`
   (SPIR-V -> HLSL -> SM 5.1 DXBC) and `D3D12Scene` (one root signature for every
   program, pipeline cache, upload-heap meshes, per-draw texture tables) draw a real
   engine shader (`-d3d12-scenetest`, section 3.3). Same day, step 1b: the
   **measurement of the whole `.sob` set** (`-d3d12-shadertest`, section 3.5)
   pushed all 7744 stages of the 116 libraries through the path and found three
   real defects - SM 5.0 vs `NonUniformResourceIndex`, FXC's X3596 on unbounded
   sampler tables, and the b13/space 0 remap collision - all fixed in the compiler
   (section 4). Step 2, the Eden scene itself:
   `EdenRenderer`/`EdenWorld`/`EdenSceneView` pointed at `D3D12Scene` (the
   `EdenProgram` -> pipeline cache, `fillConstants` into the b13 space1 root
   constants, the constants UBO as the b0 CBV, and the **sampler heap** as a
   sampler descriptor table - section 4's last paragraph), then the render targets
   / shadow / reflection / post passes, and then the 3D game modes (still refused
   with one log line).
5. **D3D11**: the same translated HLSL, a thin device; it is the cheap one once (4) exists.
6. **D3D9**: the original bytecode path (a near pass-through), with SM 3.0 limits documented
   where the engine's own shaders cannot follow.

Next step, precisely: `source/render/eden/eden_renderer.cpp` - `createProgram`'s
`PipelineDesc` handed to `D3D12Scene::getPipeline` and `recordPacket`'s push constants
handed to `D3D12Scene::draw`, with `EdenProgram`'s `pVS`/`pPS` (`TranslatedShader`)
going through `D3D12ShaderCompiler`. The tally (section 3.5) has already answered the
question that came before it - the shaders compile - and it added one item to the
same work: the **sampler heap** (a shader-visible sampler descriptor heap plus a
sampler range in the root signature), because DXSO's sampler is a runtime descriptor
array and D3D12 cannot index static samplers dynamically.

## 6. Known gaps and traps

- **`D3D12_VERTEX_BUFFER_VIEW::SizeInBytes` is the whole buffer, not one
  vertex.** Passing the stride makes the input assembler treat the buffer as a
  single vertex and drop every triangle whose indices go past it - with no error
  and a clean debug layer. It cost the scene test its first "black frame"
  (`D3D12Scene::Mesh::VertexBytes` exists for this reason).
- **A descriptor copy source must be a non shader-visible heap.** Building a
  per-draw texture table with `CopyDescriptorsSimple` from the *shader-visible*
  SRV heap is error #654 (`SRC points to a descriptor heap type that is CPU write
  only`), and on the NVIDIA driver the fault that follows is an access violation
  inside `nvwgf2umx` - i.e. it reads as a driver crash, not as a misuse. The
  device keeps a shader-opaque mirror heap (`D3D12Device::pSrvStorageHeap`, every
  texture has `SrvStorageCpu`); tables are filled from there.
- **`SetGraphicsRootSignature` before `SetGraphicsRootDescriptorTable`.** A table
  set first is error #708, and again the driver fault comes later than the
  mistake.
- **SM 5.0 has 14 constant buffer slots (b0..b13).** `register(b15)` fails to
  compile (`maximum cbuffer exceeded`), so the push constant block lives at b13.
- **A D3D9 shader will not translate at SM 5.0.** DXSO selects its sampler with a
  register operand, so its SPIR-V uses `NonUniformResourceIndex`, and SPIRV-Cross
  refuses that below 5.1 (*"Shader model 5.1 or higher is required to use bindless
  resources or NonUniformResourceIndex"*). At SM 5.0 **5523 of 7744** `.sob` stages
  failed on exactly that line; SM 5.1 DXBC is valid D3D12 input, so the fix costs
  nothing. (Section 3.5 has the runs.)
- **FXC rejects the SM 5.1 sampler table by default** - error X3596,
  *"By default, unbounded size descriptor tables are disabled to support frame
  captures in graphics tools ... can be enabled ... using switch:
  /enable_unbounded_descriptor_tables"*. SPIRV-Cross declares DXSO's sampler as a
  runtime (unbounded) descriptor array, so `D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES`
  is required, not optional.
- **The push constant register is also a SPIRV-Cross remap key.** DXSO numbers its
  images by D3D9 sampler slot (0..15), so any shader that samples through `s13` has
  a resource at (set 0, binding 13) - the same key as the push constant block - and
  `to_resource_register()` takes the image's unset cbv remap: the root constants
  land at **b0**, colliding with the constant file (FXC X4610) or compiling with
  silently wrong constants. The block therefore lives at **b13, register space 1**
  (`D3D12ShaderCompiler::kPushConstantSpace`), and the root signature's
  `32BIT_CONSTANTS` parameter names the same space. Found by the tally: 1146 of the
  first 1470 dumped stages had b0.
- **DXSO's sampler is a runtime descriptor array, so static samplers cannot draw
  Eden.** `SamplerState sampler_heap[] : register(s0, space0)` indexed by a value
  from the push constant block (`render_state_s0_s1_idx` ...) is the D3D9 sampler
  set; D3D12 cannot index static samplers dynamically. The scene's eight static
  samplers are enough for the engine's GLSL shaders and the 2D layer, and step 2
  needs the Vulkan `samplerHeap` equivalent: a shader-visible sampler descriptor
  heap, a per-material fill, and a sampler range as the last root parameter.
- **The front face is decided in screen space, where Y points down.** A Vulkan
  pipeline with `frontFace = counter-clockwise` maps to
  `D3D12_RASTERIZER_DESC::FrontCounterClockwise = FALSE`. Not yet exercised: the
  scene test culls nothing on purpose, until Eden's meshes set the convention.
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
  A third flavour, measured 2026-11-17 on the RTX 5070 Ti: **without a debugger the
  process can fault instead of stalling** (access violation inside `nvwgf2umx`,
  Windows Error Reporting picks it up, exit code 1) *after* the last frame and
  after the screenshot is written - including for the plain 2D front end run that
  draws correctly at ~4000 fps. The *same command line under `cdb`* exits 0
  (`-renderer-selftest: PASSED`), so the frame, the draws and the teardown order
  are not what is broken; it is the driver/shutdown path. `cmd /c "cdb -g -G -c
  \"g; kb 30; q\" opentdu2-cline.exe ..."` is the workaround for automation until
  `TestDriveGameInstance::terminate` is traced.
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


