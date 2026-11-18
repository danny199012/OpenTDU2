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
| Sampler heap (DXSO's runtime sampler array) | sampler set (`samplerHeap`) | **done** (`-d3d12-samplertest`, section 3.6) | - | - |
| Constant buffers b0..b3 per stage + D3D9 material state | descriptor sets + push constants | **done** (`-d3d12-constest`, section 3.7) | - | - |
| **An original `.sob` shader draws** (`-d3d12-edentest`) | `EdenRenderer` | **done for one material** (section 3.8) | - | - |
| **A real `.3DG` model draws** (`-d3d12-3dgtest`: scene + geometry + materials + textures, every node) | `EdenWorld`/`EdenSceneView` | **done** (section 3.13) | - | - |
| **The Eden draw path as a class** (`D3D12Eden`: program pair, mesh, pipeline cache, constants, texture table, draw) | `EdenRenderer::draw` | **done** (`-d3d12-3dgtest -instances N`, section 3.14) | - | - |
| **A world-scale frame** (25,600 draws: constant ring, table cache, CPU record) | n/a | **measured** (section 3.14; the ring is 256 MB/frame slot) | - | - |
| **Vertex/pixel interface** (a `.sob` shader may skip a varying register) | locations, which match by construction | **done** (`compilePair`, section 3.9) | - | - |
| Every Eden shader stage compiled (`-d3d12-shadertest`) | n/a | **done** (section 3.5) | - | - |
| **Catalogue-wide pipeline tally** (`-d3d12-pipetest`: every library's VS/PS pair → a real PSO) | n/a | **done** (section 3.10) | - | - |
| **Multi-stream meshes** (up to 3 vertex buffers, the shape of a `.3DG` node) | mesh streams | **done** (section 3.11, byte-identical frame) | - | - |
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
| `-d3d12-samplertest` | with the self test: a shader that indexes the D3D9 sampler heap dynamically, two quads, read back and measured (section 3.6) |
| `-d3d12-constest` | with the self test: the per stage constant buffers b0..b3 and the D3D9 blend state, two quads and a blend pass, read back and measured (section 3.7) |
| `-d3d12-edentest [<text>]` | the first *original* TDU2 shader on D3D12: loads Euro/Shaders, picks a simple permutation of a real library, fills the constants with the shared filler and draws a quad (section 3.8; needs `-game`). `<text>` keeps only libraries whose name contains it (section 3.9) |
| `-d3d12-shadertest [<text>]` | in the game bootstrap (`-game`): every `.sob` stage through the scene shader path, with a tally; `<text>` keeps only libraries whose name contains it (section 3.5) |
| `-d3d12-pipetest [<text>]` | in the game bootstrap (`-game`): every library's VS/PS pair compiles, links and creates a real PSO, with a tally; `<text>` keeps only libraries whose name contains it (section 3.10) |
| `-d3d12-3dgtest [<bank>]` | in the game bootstrap (`-game`): a bank's `.3DD` + `.3DG` + `.2DM` + `.2DB` drawn as a model - every node, its own `.sob` permutation, its own textures - through the production draw class (`D3D12Eden`), and the frame measured (sections 3.13/3.14); `<bank>` is a path relative to the game root, or a fragment of `4x4` / `300_c_srt8` / `2cv` |
| `-instances N` | with `-d3d12-3dgtest`: draw the model N times across a grid (a world frame's shape) and report the constant ring, the descriptor tables and the CPU record time (section 3.14) |
| `-d3d12-splitstreams` | with `-d3d12-scenetest`: upload the cube as two vertex streams - the frame must be byte-identical (section 3.11) |
| `-d3d12-meshconvtest` | no game data, no device: the `.3DG` stream expansion (SNORM / UScaled -> floats), exit 0/1 (section 3.12) |
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
D3D12 scene: root signature ready (b0 constants, b13 space1 root constants, t0..t15, s0..s15 sampler table, 14 heap entries)
D3D12 scene test: mesh.vert -> 1780 B DXBC (32 root constants), mesh.frag -> 2660 B DXBC (1 texture(s), 0 sampler(s))
D3D12 scene: pipeline 1 (4 attributes, 1 stream(s), blend 0, cull 0, depth test+write)
D3D12 scene test: 30 frame(s) of a 36 index cube on 'NVIDIA GeForce RTX 5070 Ti' (1280x720)
```

(Both log lines are from the sampler-heap build 223; the DXBC sizes grew with the
SM 5.1 switch of step 1b, the frame did not - see below.)

The frame is checked by numbers, not by eye: **10.26 % of the pixels differ from
the clear colour, 28992 distinct colours**, and the three visible faces carry
different brightness (the `+Y` face lit, the two sides dimmer) - i.e. the vertex
layout, the 128-byte root constant block, the texture table with its sampler heap
and the depth test all work. `docs/images/d3d12_scene_test.webp` is that
frame. The 28992 colours are the 64x64 checker through linear filtering plus the
per-pixel lighting, not noise: the mean difference between neighbouring pixels
inside the cube is 6.2/765.

The same frame is **byte-identical** (`SHA256 E505B37F...778CEB`) before and after
the SM 5.0 -> 5.1 switch, the push constants' move to register space 1 (section
4) and the static samplers' replacement by the sampler heap (section 3.6) - the
compiler and the sampler plumbing changed, the pixels did not. Reproduce the
numbers with:

```python
from PIL import Image
im = Image.open(r'E:\Cline\_dx_scenetest.ppm').convert('RGB'); px = list(im.getdata()); bg = px[0]
print('distinct', len(set(px)), 'diff %.2f%%' % (100.0 * sum(1 for p in px if p != bg) / len(px)), bg)
```

`-d3d12-dump-shaders <dir>` writes the HLSL next to the run; reading
`pipeline1_vs.hlsl` is how the first "black frame" was diagnosed as a
`SizeInBytes` mistake rather than a shader problem (section 6).

### 3.14 The Eden draw path, at world scale (`-d3d12-3dgtest -instances N`)

`source/render/d3d12/d3d12_eden.{h,cpp}` is the class `EdenRenderer::draw` will
call: `D3D12Eden` compiles a permutation's pair once (`compilePair`), uploads a
primitive once (`D3D12PrepareMesh` + `createMeshStreams`), caches a pipeline per
(program, mesh declaration, material state, cull), and records a draw with the
*shared* constant filler (`FillEdenConstants`) and the material's textures bound
where the shader reads them (the register map of 3.13). The `.2DM` →
`EdenMaterial` half stays in `EdenRenderer::setupMaterial`, which is backend
independent.

`-d3d12-3dgtest` now goes through it - its per-primitive pipeline / constant /
texture code is gone - and `-instances N` draws the same bank N times across a
grid in the camera's plane (`world * T(offset)`, the row-vector order: after the
instance's own transform), depth tested against each other. That is the shape a
world frame has (one material, thousands of draws) and it answers the question
3.13 left open: how many draws a frame can hold.

```powershell
build-cline\bin\opentdu2-cline.exe -d3d12-3dgtest -instances 512 `
    -game C:\TDU2-Gamedata\TestDrive2 -userdata E:\Cline\_ud-dx `
    -renderer-shot E:\Cline\_3dg_i512.ppm -log E:\Cline\_3dg_i512.log
```

Measured (RTX 5070 Ti, build 232, 1280x720, the 4x4 bank - 25 draws per
instance):

| instances | draws/frame | constant bytes copied | in the ring | texture tables | CPU record |
|---|---|---|---|---|---|
| 1 | 25 | 205,600 | 230,400 (0.1 %) | 570 hit / 180 miss, 6 resident | 0.10 ms |
| 64 | 1,600 | 13,158,400 | 14,745,600 (5.5 %) | 47,820 / 180, 6 resident | 4.47 ms |
| 256 | 6,400 | 52,633,600 | 58,982,400 (22.0 %) | 191,820 / 180, 6 resident | 11.99 ms |
| 512 | 12,800 | 105,267,200 | 117,964,800 (43.9 %) | 383,820 / 180, 6 resident | 23.17 ms |
| 1024 | 25,600 | 210,534,400 | 235,929,600 (87.9 %) | 767,820 / 180, 6 resident | 53.04 ms |

The one-instance frame is **byte-for-byte the 3.13 result** (57,442 px, mean
219.6, 570/180 tables) - the module changed the code, not the pixels - and the
other two banks reproduce their numbers too (SRT8 43,844 px / 22 draws, the
beach playhouse 26,359 px / 33 draws / 32 resident tables).

What this measured, and what it changed:

- **A draw costs 9,216 bytes of the constant ring** (8,224 copied: the VS file
  4,368 bytes - 16 int4 registers + 256 float4 + the bool bitmask - and the PS
  file 3,856; plus the 64-byte specialization block, which *both* stages bind,
  and every block rounded up to the 256-byte alignment a root CBV needs). The
  files cannot be shortened to what a program happens to name: the shader's
  `cbuffer_t` declares those offsets, and the bitmask sits at the *end*. The
  number is therefore a budget: the ring's original 64 MB (an estimate of
  "~7,600 draws") held **7,281** draws, and the 512-instance run (12,800 draws)
  needed **118 MB, 176 % of it** - it logged `the per frame constant ring (64 MB)
  is full` once per frame and lost its last ~40 % of draws to the zero block,
  while its coverage test still *passed*. A probe that only looks at pixels
  cannot see this, which is why the ring's own number is printed. The ring is now
  **256 MB per frame slot** (~29,000 draws): a world's main pass is ~7,600 draws
  and the Z pre-pass draws the same geometry again, and the Vulkan renderer's
  per-frame transient buffer is 512 MB for the same reason (two D3D12 slots now
  cost what one Vulkan buffer did).
- **The descriptor-table cache holds at world scale.** 25,600 draws bind 6
  tables, not 25,600: 767,820 hits and 180 misses. The 180 misses are one per
  distinct set per frame slot over 30 frames - the cache is rebuilt once per
  frame, as designed (3.13), and a material's set is stable within a frame.
- **~1.4 us per draw, single-threaded** - after the per-draw string building came
  out of the path. The first version classified a stage's cbuffer slots with a
  `std::string` per draw (`ToUpper` + `find` + `+=`) and built the
  warned-parameter `unordered_set` inside `draw`: 25,600 draws took 53.04 ms.
  Classifying the slots once per program (`D3D12EdenProgram::VSKinds` /
  `PSKinds`) and keeping the warning set on the renderer (cleared in
  `beginScene`, like `EdenRenderer`) brought that to **35.72 ms - 33 % less, with
  a byte-identical frame** (78,784 px, the same table counts). A 7,600-draw world
  pass is therefore ~10.6 ms of recording, which is inside a frame but leaves no
  room for the shadow / reflection passes: the Vulkan renderer records in
  parallel (`EdenRenderer::ParallelRecord`) and the D3D12 path needs the same -
  the next performance item, and the reason this section reports the CPU time at
  all.

- **A bank probe's pipelines are per primitive** (25 pipelines for 25 draws,
  32 for the playhouse): each primitive is its own mesh, and the pipeline key
  includes the mesh declaration. In a world, primitives of one program share a
  declaration and the count is per material - which is what the cache is for.
- **A material with a unique texture set pays full price** (the playhouse: 30
  hits / 960 misses, 32 resident tables for 33 draws). The table cache only
  wins where draws repeat a set - cars, instanced props, a world's repeated
  materials. That is the right trade (a table per draw would not fit in the 4,096
  descriptor slot at all), but a *scene* of one-off materials will allocate a
  table per draw and must stay inside the slot's budget.

### 3.15 What is still not drawn

The Eden *draw* is now a Direct3D 12 class (`D3D12Eden`, section 3.14), but
`EdenRenderer` itself still talks to the Vulkan device, so `canDrawScene()` is
false on D3D12 and the 3D modes stay refused. Three things stand between the
module and a game frame, in the order they unblock each other:

1. **A D3D12 mesh source in `EdenModel`.** A model packs all its primitives into
   two buffers (`eden_model.cpp`) and hands each item a (buffer, offset, count) -
   Vulkan buffers today. `D3D12PrepareMesh` + `D3D12Eden::createMesh` are the
   per-primitive form; the packed form needs `D3D12Scene::Mesh` to take a vertex
   / index *offset* (D3D12's `BaseVertexLocation` / `StartIndexLocation`), or one
   upload per item.
2. **Render targets.** `D3D12Scene` records into the swapchain's back buffer and
   the device's depth-stencil only. The Eden scene renders the shadow atlas, the
   reflection maps and the HDR colour target and then samples them - an offscreen
   target plus a depth-as-texture SRV is the missing mechanism, and it is what
   the shadow / reflection / post passes of section 5 need.
3. **A parallel record.** ~1.4 us per draw on one thread (3.14) is ~10.6 ms for a
   world pass - inside a frame, but with nothing left for the shadow / reflection
   passes; `EdenRenderer::ParallelRecord` is the Vulkan answer and the D3D12 path
   needs its equivalent (PPO command lists, or a cheaper constant fill).

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
* the sampler - DXSO declares it as a *runtime descriptor array*, which D3D12's
  static samplers cannot serve - was the open item this tally left; it is a
  sampler descriptor heap now (section 3.6).

### 3.6 The sampler heap (`-d3d12-samplertest`)

The tally proved every shader *compiles*; it cannot prove one *draws*, and the
one access it cannot even compile into a static-sampler pipeline is the one every
Eden draw makes: DXSO hands the shader `sampler_heap[]` (a runtime descriptor
array) and the shader picks the entry with a value from the push constants.
D3D12 cannot index a static sampler dynamically, so `D3D12Scene` serves the same
fourteen entries from a shader-visible **sampler descriptor heap** bound as the
root signature's last parameter (s0..s15, section 4).

```powershell
build-cline\bin\opentdu2-cline.exe -renderer d3d12 -d3d12-samplertest -d3d12-debug `
    -renderer-selftest 120 -renderer-shot E:\Cline\_st_sampler.ppm -log E:\Cline\_st_sampler.log
```

The test is a measurement, not a look: one 2x1 two-colour texture
(`230,40,40` / `40,40,230`), one quad mesh, the same UVs in **[0,2]** for both
draws, and the *only* difference between the two draws is the heap index in the
root constants - **linear wrap (0)** on the left, **linear clamp/clamp (13)** on
the right. Wrap repeats the texture twice across the UV range, clamp stretches
the second texel over three quarters of it, so the halves' mean colours have to
differ; if the index never reached the sampler they would be identical. The frame
is read back (`D3D12Device::readbackBackbuffer`, the screenshot's own path) and
the two halves are measured.

Measured (build `223 (51af9d2+dirty) - cline`, 1280x720, debug layer on and
silent, exit 0):

```
D3D12 sampler test: 120 frame(s) of two quads on 'NVIDIA GeForce RTX 5070 Ti' (1280x720);
  wrap mean 135.0/40.0/135.0 (186624 px), clamp mean 87.5/40.0/182.5 (186624 px), dR 47.5 dB 47.5
D3D12 sampler test: PASSED (dynamic sampler-heap indexing reaches the sampler)
```

- The means are what the arithmetic predicts: 50/50 of the two texels for wrap
  (135/40/135) and 25/75 for clamp (87.5/40/182.5); the difference is 47.5 levels
  on R and B, and the pass threshold is 20.
- `186624` covered pixels is `576 x 324` - two 0.9-NDC quads of the 1280x720
  frame, i.e. the coverage check is exact, not approximate.
- The **same numbers on WARP** (`-d3d12-warp`: 135.0/40.0/135.0 and
  87.6/40.0/182.4), so the result is the API contract and not one driver's
  behaviour.
- No regression in the frame that was already there: the scene test's PPM is
  **byte-identical** to the pre-sampler-heap build
  (`SHA256 E505B37F...778CEB`), because its shader samples through s0, which is
  entry 0 - linear wrap - exactly what the static sampler it used to get was.

### 3.7 The constant buffers and the material state (`-d3d12-constest`)

The sampler heap (3.6) made the *texture* half of an Eden draw possible. The
other half is the data: DXSO's constant file for a stage is a cbuffer, and the
compiler remaps a stage's cbuffers into **b0..bN in SPIR-V order**
(`d3d12_shader.cpp`) - measured, the widest `.sob` stage has three (the D3D9
constant file, `spec_state_t`, `clip_info_t`). The root signature therefore has
**four CBVs per stage** (b0..b3, vertex and pixel), and every draw copies its
blocks into a per-frame-slot upload ring and points the root CBVs at the copy
(a root CBV is a GPU virtual address; the caller's bytes are CPU side and may be
overwritten the moment the call returns - the Vulkan path's `allocateTransient`
does the same thing).

The same commit maps **`EdenMaterial`'s D3D9 state** into the pipeline:
`D3DBLEND` source/destination factors, `D3DBLENDOP`, `D3DCOLORWRITEENABLE` bits,
`D3DCMPFUNC` and `D3DCULL` (with the mirroring-view swap and the 3 -> 1
"two sided" rule `EdenRenderer::getPipeline` applies), passed as
`PipelineDesc::D3D9State` so a material's raw values need no translation. Two
things deliberately stay *out* of the pipeline: the constant blend factor
(`D3DBLENDFACTOR`, dynamic state on D3D12 - `OMSetBlendFactor`), and the alpha
test, which DXSO already compiles into a `discard` driven by the spec constant
block and the push constant alpha reference.

```powershell
build-cline\bin\opentdu2-cline.exe -renderer d3d12 -d3d12-constest -d3d12-debug `
    -renderer-selftest 60 -renderer-shot E:\Cline\_st_const.ppm -log E:\Cline\_st_const.log
```

The probe is a measurement, not a look. One quad mesh, one pipeline, and the
shader pair has the shape an Eden draw has: **two** cbuffers in the vertex stage
(b0 a scale, b1 a per-draw offset) and **two** in the pixel stage (b0 a tint,
b2 a scale multiplier - b1 is deliberately left unfilled, so a backend that
packs blocks by order instead of by register cannot pass). Three draws:

| draw | blocks | expected |
|---|---|---|
| left quad | vs b1 = (-0.52, 0), ps b0 = red | (255, 0, 0) |
| right quad | vs b1 = (+0.52, 0), ps b0 = green, ps b2 = 0.5 | (0, 128, 0) |
| left again, blended | ps b0 = blue at alpha 0.5, SRC_ALPHA / INV_SRC_ALPHA | 0.5 red + 0.5 blue = (128, 0, 128) |

Measured (build `223 (1c326db+dirty) - cline`, 1280x720, debug layer on and
silent, exit 0; re-run unchanged on the final `224 (778e3fb+dirty)` after the
documentation-only rebuild):

```
D3D12 constant test: 60 frame(s) of two quads on 'NVIDIA GeForce RTX 5070 Ti' (1280x720);
  left mean 128.0/0.0/127.0 (373248 px), right mean 0.0/127.0/0.0 (373248 px)
D3D12 constant test: PASSED (b0/b1/b2 reached both stages and SRC_ALPHA blending applied)
```

- The means are the arithmetic: 0.5 red + 0.5 blue on the left, 0.5 green on the
  right. `373248` covered pixels is exactly `576 x 648` - a 0.9-NDC quad of each
  half of the frame - so the coverage check is exact.
  (`docs/images/d3d12_const_test.webp` is that frame: purple left, half-green
  right.)
- **The same numbers on WARP** (`-d3d12-warp`, 20 frames): `128.0/0.0/128.0` and
  `0.0/128.0/0.0`, `373248` px each. The result is the API contract, not one
  driver's blend rounding (the 1-level difference on B is UNORM rounding of
  127.5).
- **No regression, measured at the same build**: `-d3d12-scenetest`'s PPM is
  **byte-identical** to the pre-change one (`SHA256
  E505B37F0063C775...C3F778CEB`), `-d3d12-samplertest` reads the same
  135.0/40.0/135.0 vs 87.5/40.0/182.5 (dR 47.5), and the Flash front end still
  draws on D3D12 (17035 distinct colours, 54.80 % non-black - the earlier
  17037 is the movie moment, not the layer).

What the test rules out, and how:

- **"the blocks land at the wrong register"** - the left quad's position comes
  from **b1**, not b0; if the blocks were packed by order the offset would be
  read from b1 anyway... so the *pixel* stage carries the discriminating case:
  its b1 is never written, its b2 is. A backend that compacted the blocks would
  read the scale multiplier from b=1 and colour the right half at 255.
- **"a draw's constants leak into the next"** - the three draws reuse the same
  pipeline and mesh with different blocks in the same frame; the ring gives each
  its own range, and the halves stay distinct frame after frame (60 of them).
- **"the D3D9 state is the four `eBlendMode` cases in disguise"** - the blended
  quad is SRC_ALPHA / INV_SRC_ALPHA with a 0.5 source alpha, which no
  `eBlendMode` value expresses together with an opaque pass in one pipeline
  cache; an unwritten blend state leaves the left half at 255 red.

### 3.8 An original shader draws (`-d3d12-edentest`)

The probes above measure pieces. This one is the first *end to end* draw of a real
TDU2 material shader on Direct3D 12: a permutation taken from a shipped `.sob`
library, its D3D9 constant file written by the renderer's own filler, its texture
in the t0 table, its state in the pipeline.

```powershell
build-cline\bin\opentdu2-cline.exe -renderer d3d12 -d3d12-edentest -d3d12-debug `
    -game C:\TDU2-Gamedata\TestDrive2 -userdata E:\Cline\_ud-dx `
    -renderer-shot E:\Cline\_st_eden.ppm -log E:\Cline\_st_eden.log
```

It runs in the bootstrap next to `-d3d12-shadertest` (it needs the file service
for `Euro/Shaders` and creates its own hidden window and device), picks the
permutation itself - the first one of a candidate library that is unskinned,
opaque, not the depth / shadow-caster pass and samples exactly one texture -
compiles both stages, fills `FillEdenConstants` into the VS/PS constant files
(`b0`), binds the engine's checker into `t0` and draws a quad into a magenta
frame, so any pixel the shader painted is unambiguous.

Measured (build `225 (82fa215+dirty) - cline`, 1280x720, debug layer on and
silent):

```
D3D12 Eden test: TD_Lamp vs5/ps1 (one texture, unskinned, opaque); cbuffers vs [c, spec_state, clip_info]
  b0=file, b1=spec, b2=zero, ps [c, D3D9SharedPS, spec_state] b0=file, b1=zero, b2=spec
D3D12 Eden test: 30 frame(s) of a quad on 'NVIDIA GeForce RTX 5070 Ti' (1280x720);
  110224 px covered (11.96 %), mean 43.0/48.0/57.5; 6 of 256 vs and 0 of 224 ps registers written
D3D12 Eden test: PASSED (the original shader painted the quad on Direct3D 12)
```

- The frame holds **47 colours**: the checker's two shades, lit, at *exactly*
  `43216` pixels each - `(36, 40, 48)` and `(50, 56, 67)`, i.e. the 64x64 8-pixel
  checker `TextureCache` uploads, sampled through the sampler heap and modulated
  by the ambient + directional constants (this shader's material parameters are
  its own; the probe has no `.2DM`, see below). `docs/images/d3d12_eden_test.webp`
  is that frame.
- `110224` covered pixels is the arithmetic of the probe's camera: a quad of +-0.8
  world units three metres in front of a 60-degree vertical FOV is
  `(2 * 0.8 / (tan(30) * 3 * 1.777)) * (2 * 0.8 / (tan(30) * 3))` of the frame.
- **WARP reads the same frame**: same library, same `110224 px`, mean
  `43.0/48.0/57.5`. The constants, the sampler heap and the pipeline state are the
  API contract, not one driver's arithmetic.
- **The constants are the renderer's own**: `EdenRenderer::fillConstants` became
  `FillEdenConstants` (`source/render/eden/eden_constants.h`) with an explicit
  context - the scene, the derived view-projection, the precise variant, the
  epoch and the warning set - and the renderer is a thin wrapper around it. The
  constant-file layout (`int4 c_i[16]; float4 c_f[N];` + bool mask) and the spec
  block's size moved there as `kEden*ConstantsSize`, so a size cannot drift
  between the backends. `FillEdenSpecConstants` writes the
  D3D9SpecializationInfo block for both.
- **The cbuffer map is read, not guessed**: `D3D12ShaderStage::ConstantBufferNames`
  records what RemapResources put at each register, and the probe binds by shape
  (the specialization state -> spec block, clip/shared -> the zero block, anything
  else -> the constant file). The names are SPIRV-Cross's *instance* names (`c`,
  `spec_state`, `clip_info`, `D3D9SharedPS`), not the HLSL type names - see
  section 6.
- **No regression**: `-d3d12-scenetest`'s PPM is byte-identical
  (`SHA256 E505B37F...778CEB`), `-d3d12-samplertest` and `-d3d12-constest` read
  their previous numbers, and a Vulkan `-notitlescene` frame still renders
  normally (17105 distinct colours, 55.17 % non-black - the extra colours against
  the documented 17095 are the movie moment, not the filler: the refactor is a
  code move, and the diff shows it).

**The open gap this probe found** - `TD_LightBeam vs2/ps1` compiled but
`CreateGraphicsPipelineState` rejected it with `E_INVALIDARG` - is closed in
section 3.9. It was not the vertex bytecode: the two stages' varying slots
disagreed, and the debug layer had been saying so all along.

### 3.9 The vertex/pixel interface (`-d3d12-edentest TD_LightBeam`)

Section 3.8 ended on a refused pipeline and two wrong conclusions, both now
measured. The flag takes an optional library filter, so either pair can be drawn
on its own:

```powershell
build-cline\bin\opentdu2-cline.exe -renderer d3d12 -d3d12-edentest TD_LightBeam ``
    -d3d12-debug -verbose -d3d12-dump-shaders E:\Cline\_dx_probe\pad ``
    -game C:\TDU2-Gamedata\TestDrive2 -userdata E:\Cline\_ud-dx ``
    -renderer-shot E:\Cline\_dx_probe\eden_beam.ppm -log E:\Cline\_dx_probe\eden_beam.log
```

**There was a message.** `D3D12Device::flushDebugMessages` skipped every message
below `D3D12_MESSAGE_SEVERITY_WARNING`, and the severities count *up* from
CORRUPTION (0) / ERROR (1) / WARNING (2) - so it dropped exactly the errors. With
the filter fixed the device says what it always knew:

```
D3D12 debug (client): ID3D12Device::CreateGraphicsPipelineState: Vertex Shader - Pixel Shader
  linkage error: Signatures between stages are incompatible. Semantic 'TEXCOORD' is
  defined for mismatched hardware registers between the output stage and input stage.
```

**It was not the vertex bytecode.** D3D12 links the stages by varying *slot* -
the k-th non-system-value output against the k-th input - while HLSL assigns
those slots densely in declaration order. DXSO's locations are D3D9's varying
*registers*, and a `.sob` shader may skip one: TD_LightBeam's VS writes oT0..oT4
and oT7 (reflected: TEXCOORD0,1,2,3,4,7 plus DXSO's synthetic 10, 11 - eight
slots), its PS reads t0,t1,t2,t7 - four slots, so TEXCOORD7 lands in slot 3
instead of slot 5. A permutation whose interface is dense on both sides
(`TD_Lamp`: TEXCOORD0,1) links by accident, which is why the first probe worked.

**The fix is `D3D12ShaderCompiler::compilePair`.** The pair is compiled together;
both signatures are reflected from the compiled DXBC (`D3DReflect`, system values
dropped, ordered by slot) and, when they disagree, the pixel stage's
`SPIRV_Cross_Input` is rebuilt with an unused `float4` pad for every vertex
output it does not read, then recompiled and re-reflected. Three properties of
FXC were measured before choosing that shape (each is a trap in section 6):

| probe | measured |
|---|---|
| does FXC keep an unused pad input? | yes - the entry stays in the input signature with an **empty `Used`** mask |
| pad width | `float4` only: two `float` pads were packed into **one** register (x and y of register 3) |
| a system value first in the struct? | it takes the slot: `SV_Position` then `TEXCOORD0` -> registers 0 and 1, so the pads are emitted *before* the system values |

Measured (build `226 (bed0c1a+dirty) - cline`, RTX 5070 Ti, debug layer on):

```
D3D12 shader TD_LightBeam: pixel stage relinked to 8 slot(s) (3 pad): vs [TEXCOORD0@0
  TEXCOORD1@1 TEXCOORD2@2 TEXCOORD3@3 TEXCOORD4@4 TEXCOORD7@5 TEXCOORD10@6 TEXCOORD11@7],
  ps [TEXCOORD0@0 TEXCOORD1@1 TEXCOORD2@2 TEXCOORD3@3 TEXCOORD4@4 TEXCOORD7@5 TEXCOORD10@6 TEXCOORD11@7]
D3D12 Eden test: TD_LightBeam vs2/ps1 (one texture, unskinned, opaque); cbuffers vs [c, spec_state, clip_info]
  b0=file, b1=spec, b2=zero, ps [c, D3D9SharedPS, spec_state] b0=file, b1=zero, b2=spec
D3D12 Eden test: 30 frame(s) of a quad on 'NVIDIA GeForce RTX 5070 Ti' (1280x720); 110224 px
  covered (11.96 %), mean 0.0/0.0/0.0; 18 of 256 vs and 10 of 224 ps registers written
D3D12 Eden test: PASSED (the original shader painted the quad on Direct3D 12)
```

- The relink line is the proof: the two **reflected** lists are identical, so the
  pixel stage's used semantics sit in the slots the vertex stage writes. A PSO is
  only created from that state.
- `110224 px` is the probe camera's exact quad arithmetic (section 3.8), i.e. the
  rasteriser is running the padded struct and the pads did not shift the fields.
- **The quad is black, and that is the shader's answer**, not a broken binding:
  the beam's PS decodes its colour out of the texture and multiplies it by the
  beam constants, and this material has no `.2DM` (its registers are the 1.0
  stand-in) over a synthetic quad. `-d3d12-edentest TD_Lamp` is the colour case
  and the regression A/B for the padded path - the same build still reads
  `110224 px`, mean **43.0/48.0/57.5**, **47 colours**, the two lit checker shades
  at exactly `43216 px` each, i.e. section 3.8's numbers unchanged.
- The probe's synthetic mesh now gives every TEXCOORD `w = 1` - the beam's PS
  does `1.0 / t1.w` for a projected lookup, and the old `w = 0` painted the whole
  quad black before the interface was even involved (a second, independent
  reason for black that the fix would otherwise have hidden).
- `getPipeline`'s failure line now prints the two varying lists as well
  (`D3D12ShaderCompiler::describeVaryings`), so a refused PSO names its interface
  even when the debug layer is off.
- **WARP reads the same frame**: `-d3d12-warp` gives the same relink line
  (8 slots, 3 pads), the same `110224 px` and the same `18/10` register counts, so
  the padded interface is the API contract, not one driver's packing.
- **No regression**: `-d3d12-scenetest`'s PPM is byte-identical (`SHA256
  E505B37F...C3F778CEB`), `-d3d12-samplertest` still reads 135.0/40.0/135.0 vs
  87.5/40.0/182.5, `-d3d12-constest` passes, the Flash front end still draws
  (54.80 % non-black, debug layer silent) and a Vulkan `-notitlescene` frame
  renders normally.

`-d3d12-dump-shaders` shows what the padded pixel stage looks like:

```hlsl
struct SPIRV_Cross_Input
{
    float4 in_Texcoord0 : TEXCOORD0;
    float4 in_Texcoord1 : TEXCOORD1;
    float4 in_Texcoord2 : TEXCOORD2;
    float4 pad3 : TEXCOORD3;
    float4 pad4 : TEXCOORD4;
    float4 in_Texcoord7 : TEXCOORD7;
    float4 pad10 : TEXCOORD10;
    float4 pad11 : TEXCOORD11;
};
```

What this does **not** cover: a pixel stage that reads a register the vertex stage
does not write at all. D3D9 leaves that undefined, and `compilePair` refuses such
a pair and names both lists rather than inventing a pad. How often that happens
in the catalogue is exactly what the next measurement - a *pipeline* tally
(compile every library's pairs and create a PSO for each) - will say.

### 3.10 The catalogue-wide pipeline tally (`-d3d12-pipetest`)

Section 3.9 ended on a measurement it could not make: how often a pair's
interfaces cannot be lined up *in the catalogue*. That is the number that
decides whether `EdenRenderer` can be pointed at the backend, so the tally walks
every library the way the libraries themselves encode their permutations - each
pixel item paired with the vertex item that carries the same hashcode - and for
each pair:

1. `D3D12ShaderCompiler::compilePair` (SPIR-V -> HLSL -> DXBC, then the varying
   link of section 3.9),
2. `D3D12BuildVertexLayout` from the vertex shader's dcl list (section 3.11),
3. `D3D12Scene::getPipeline` - a real `CreateGraphicsPipelineState` on a real
   device.

```powershell
build-cline\bin\opentdu2-cline.exe -renderer d3d12 -d3d12-pipetest [<text>] `
    -game C:\TDU2-Gamedata\TestDrive2 -userdata E:\Cline\_ud-dx `
    -log E:\Cline\_pipe.log      # -verbose: a line per pair, which is how a slow library names itself
```

It draws nothing and binds nothing: blend, cull, depth and the write mask cannot
make the device refuse a PSO, so every pair is built with the opaque D3D9 state,
and textures / samplers / constants are not part of a PSO at all. A *permutation*
is a spec switch set, not a program: the bytecode a pair compiles to is a
function of its two attrib blocks, so each distinct (VS attrib, PS attrib) is
measured once and the repeats are counted - `TD_Building`'s 11376 permutations
collapse to 4429 programs, `TD_Bones`' 48 to 1, `TD_CarMat_NM`'s 2560 to 1680.
The cache is flushed every 512 creations (`releasePipelines`) so
the tally never holds the catalogue's compiled state, and the watchdog beats
once per pair - a library takes minutes.

`-d3d12-pipetest TD_Lamp` on its own, the smoke test that the harness works:

```
D3D12 pipetest: TD_Lamp                         24 pair(s),    16 repeat(s):    8 pipeline(s),   0 refused,   0 link,   0 compile,   0 unpaired,   0 screen-space
D3D12 pipetest: 1 libraries (0 one-sided), 24 pair(s) (16 repeat(s) collapsed to distinct programs): 8 pipeline(s) accepted, 0 refused by the device, 0 link failure(s), 0 compile failure(s), 0 unpaired pixel item(s) in 0.5 s
D3D12 pipetest: 0 screen-space pair(s) (empty input layout), 0 format(s) that need a converted stream (A2B10G10R10 SNORM / UBYTE4 USCALED)
D3D12 pipetest: PASSED (8 of 8 distinct program(s) created a pipeline)
```

The whole catalogue, one run (build `227 (a0cbfca+dirty) - cline`, RTX 5070 Ti):

```
D3D12 pipetest: TD_Building                  11376 pair(s),  6947 repeat(s): 4429 pipeline(s),   0 refused,   0 link,   0 compile,   0 unpaired,   0 screen-space
D3D12 pipetest: TD_Bones                        48 pair(s),    47 repeat(s):    1 pipeline(s),   0 refused,   0 link,   0 compile,   0 unpaired,   0 screen-space
D3D12 pipetest: TD_CarMat_NM                  2560 pair(s),   880 repeat(s): 1680 pipeline(s),   0 refused,   0 link,   0 compile,   0 unpaired,   0 screen-space
... 113 more lines ...
D3D12 pipetest: 116 libraries (0 one-sided), 31470 pair(s) (15365 repeat(s) collapsed to distinct
  programs): 16105 pipeline(s) accepted, 0 refused by the device, 0 link failure(s), 0 compile
  failure(s), 0 unpaired pixel item(s) in 2986.9 s
D3D12 pipetest: 0 screen-space pair(s) (empty input layout), 0 format(s) that need a converted
  stream (A2B10G10R10 SNORM / UBYTE4 USCALED)
D3D12 pipetest: PASSED (16105 of 16105 distinct program(s) created a pipeline)
```

**Every distinct program TDU2 ships compiles, links and creates a pipeline**:
16105 of 16105, no refusal, no unlinked pair, no stage that failed to compile -
in 49.8 minutes on one run, because a PSO per program is real driver work. Two
notes on that line: the `0 format(s)` is the *declaration-only* layout's count
(the storage formats are a property of the mesh, section 3.12), and every padded
pair still logs its relink line (4704 of them), which is the evidence section 3.9
rests on and the reason the log is 1.4 MB.

**Defect 1 - the pixel stage may stop early, but its SV_Position may not.** The
first full run refused **every one of TD_Accessories2's programs (32 of its 64
permutations) and all 16 of TD_Avatar's** with `E_INVALIDARG`, and the reflected
lists looked aligned:

```
CreateGraphicsPipelineState failed (0x80070057); vs outputs [TEXCOORD0@0 TEXCOORD1@1
  TEXCOORD2@2 TEXCOORD3@3 TEXCOORD4@4 TEXCOORD5@5 TEXCOORD6@6 TEXCOORD7@7 TEXCOORD8@8
  TEXCOORD10@9 TEXCOORD11@10], ps inputs [TEXCOORD0@0 ... TEXCOORD8@8]
D3D12 debug (client): CreateGraphicsPipelineState: Vertex Shader - Pixel Shader linkage
  error: ... Semantic 'SV_Position' is defined for mismatched hardware registers ...
```

Every generic semantic matched - the pixel stage simply read 9 of the 11
varyings. But D3D12 compares the **hardware register** of every parameter, and a
system value takes the one after the generics: SPIRV-Cross emits
`gl_Position : SV_Position` *last* in the vertex stage's output struct (measured:
`_dx_probe\pad\pipeline1_vs.hlsl`), so the vertex stage's SV_Position is register
11 while a pixel stage that stops at TEXCOORD8 puts its own in register 9. The
old `VaryingsLineUp` accepted a pixel list that was a *prefix* of the vertex
list, so no pads were emitted. It now requires the same length, and
`LinkVaryings` pads - `TD_Accessories2` 64/64 and `TD_Avatar` 16/16 after the
fix.

That change alone regressed `TD_Bones` (48/48 -> 0/48, "the shader interface
could not be reflected"): its pixel stage reads *no* varying (a flat colour), so
the padding path treated an empty pixel interface as a failed reflection. A
pixel stage with an empty `struct SPIRV_Cross_Input` is legitimate and gets pads
like any other; one that takes *no parameter at all* (not even SV_Position) has
no register to align and needs none. `TD_Bones` is 48/48 again.

**Defect 2 - the pipeline key identified stages by address.** `MakePipelineKey`
appended the address of each stage's `Bytecode` vector (the Vulkan cache's
lifetime rule), which is stable only while the stage outlives the pipeline. The
tally compiles thousands of pairs into locals that reuse their stack addresses,
so two different programs would have hashed to one key and the second would have
silently reused the first one's PSO - a false "accepted". The key is now an
FNV-1a hash of both blobs plus their sizes; `-d3d12-scenetest`'s frame is
unchanged by it (`SHA256 E505B37F...C3F778CEB`, section 3.11).

### 3.11 Multi-stream meshes (`-d3d12-splitstreams`)

A `.3DG` node's vertices are not one interleaved buffer: the engine splits them
over up to three streams (position / normal / colour, texture coordinates /
weights, per-vertex bone data), each with its own stride, and every dcl says
which stream it reads. `D3D12Scene::Mesh` therefore carries
`kMaxStreams = 3` vertex buffers and `draw` binds them with a single
`IASetVertexBuffers`, and `D3D12BuildVertexLayout`
(`source/render/d3d12/d3d12_vertex_layout.*`) turns a vertex shader's dcl list
plus a mesh's own declaration into the pipeline's input layout:

| source | what it supplies |
|---|---|
| the vertex shader's `TranslatedShader::Inputs` (the `.sob` dcl) | which semantics the stage reads, and the SPIR-V location that becomes the semantic index |
| the mesh's `RawVertexAttribute` list (the `.3DG`) | the stream, the storage format (`VAF_*` -> DXGI) and the byte offset |
| a dcl the mesh does not carry | one `float4` in stream 3 at the usage's offset (COLOR 16, NORMAL 32, TANGENT / BINORMAL 48, else 0) - Direct3D 12 reads an unbound stream as zero, where Vulkan leaves it undefined |

The A/B is the scene test's own cube, uploaded twice - once interleaved (stride
40) and once split into two streams (stride 40 and 8):

```powershell
build-cline\bin\opentdu2-cline.exe -renderer d3d12 -d3d12-scenetest `
    -renderer-selftest 30 -renderer-shot E:\Cline\_st_stream1.ppm
build-cline\bin\opentdu2-cline.exe -renderer d3d12 -d3d12-scenetest -d3d12-splitstreams `
    -renderer-selftest 30 -renderer-shot E:\Cline\_st_stream2.ppm
```

Both frames are `SHA256 E505B37F0063C7750BF39A9DC4579FD4E97937722B5DC825B7C1470C3F778CEB`
- the hash every earlier scene-test build produced - so the two-stream draw is
the *same* draw, and the pipeline-key change of section 3.10 did not move a
pixel. The log names the mode (`two vertex streams (stride 40 and 8)`), and the
pipeline line says `2 stream(s)`.

Three measured facts about the layout, all visible in the logs:

- **A wrong format is a warning, not a refusal.** With the declaration-only
  layout (the tally, where no mesh exists) a `BLENDINDICES` dcl is guessed as
  `R8G8B8A8_UINT` while the HLSL reads a float; the debug layer prints
  *"...specifies mismatched format: 'R8G8B8A8_UINT'. This is not an error,
  since behavior is well defined..."* and the PSO is created. The element
  format only decides the conversion; the *semantic* decides the linkage.
- **A dcl the mesh lacks still needs an element** - `CreateGraphicsPipelineState`
  refuses a layout that does not cover every vertex-shader input ("Input
  signature is not compatible with the input layout"), which is why the
  fallback stream exists rather than a shorter layout.
- **Two storage formats have no DXGI equivalent** and must be converted when
  the mesh is uploaded (counted by the tally's last line, step 2b's work):
  `A2B10G10R10 SNORM` (`VAF_NORMAL_/TANGENT_A2B10G10R10_SNORM_PACK32`) is mapped
  to `R10G10B10A2_UNORM`, which loses the sign, and
  `VAF_BONEINDEX_R8G8B8A8_USCALED` to `R8G8B8A8_UINT`, which the D3D9 shader
  reads as a float. Both need a float4 expansion at upload, not a different
  pipeline.

### 3.12 The `.3DG` mesh streams (`-d3d12-meshconvtest`)

`D3D12BuildVertexLayout` reads a declaration, but the declaration a `.3DG`
primitive carries is written in the engine's storage formats, and three of them
have no DXGI equivalent at all:

| `.3DG` format | Vulkan | Direct3D 12 |
|---|---|---|
| `A2B10G10R10 SNORM` (normal, tangent) | `VK_FORMAT_A2B10G10R10_SNORM_PACK32` | no such format - `R10G10B10A2_UNORM` would drop the sign |
| `UBYTE4 USCALED` (bone index) | `R8G8B8A8_USCALED` - the byte arrives as a float | no UScaled either; `R8G8B8A8_UINT` arrives as a `uint`, which the D3D9 shader reads as a float |

`D3D12PrepareMesh` (`source/render/d3d12/d3d12_mesh_builder.*`) turns one
`GeometryPrimitive` into upload-ready data. When no attribute needs an
expansion it passes the engine's own bytes through (the common case, and the
cheap one); when one does, the primitive is rebuilt into **one** interleaved
stream where the packed SNORM is decoded to four floats (10-bit signed values
over 511, the 2-bit alpha over 1) and the bone index to its byte value, and the
other attributes keep their bytes. The declaration is rewritten with it, so the
layout builder sees ordinary float formats.

The probe needs no device and no game data - it is arithmetic, so it runs in the
bootstrap and exits with the verdict:

```powershell
build-cline\bin\opentdu2-cline.exe -d3d12-meshconvtest -nolog
```

```
D3D12 mesh convert test: PASSED (stride 52, 2 expanded attribute(s), SNORM -1 -> -1.0, UScaled 255 -> 255.0)
```

It packs `(-1, 0, 0.5)` and an alpha of `-1` into a SNORM normal, a
`(1,2,3,4)` and a `255` bone index, plus a float3 position and a float2 uv, and
checks the decoded values: `-1` must come out as `-1.0` (not `0.5`, which is
what the UNORM rounding of a wrong mapping gives, and not `0`), a 10-bit `0.5`
within one quantum (`1/511`), the 2-bit alpha as `-1`, and `255` as `255.0`
(UScaled) rather than `1.0` (UNORM). It also checks the layout arithmetic
(12 + 16 + 8 + 16, 4-byte aligned = stride 52) and the pass-through case: a
primitive without a packed format keeps its two streams and its bytes.

Writing the probe paid for itself: the first run failed on every SNORM value
because the decoder added the attribute's byte offset a second time (the caller
had already applied it), so it read past the vertex - the kind of mistake that
would have shown up much later as "the normals look wrong in the game".

### 3.13 Real game geometry (`-d3d12-3dgtest`)

The probes before this one drew synthetic geometry: a quad with an original
shader (3.8), the engine's cube (3.3). This one takes a bank out of the game
data - its `.3DD` scene, `.3DG` geometry, `.2DM` materials and `.2DB`
textures - and draws the model whole, every node with its own transform, through
its own `.sob` permutations:

```powershell
build-cline\bin\opentdu2-cline.exe -d3d12-3dgtest `
    -game C:\TDU2-Gamedata\TestDrive2 -userdata E:\Cline\_ud-dx `
    -renderer-shot E:\Cline\_3dg.ppm -log E:\Cline\_3dg.log
```

The flag takes a bank path relative to the game root
(`-d3d12-3dgtest Euro/Bnk/Islands/Ibiza/Exterior/eph_l0c.bnk`) or a fragment of
one of the default candidates (`4x4`, `300_c_srt8`, `2cv` - the generic cars
whose file names are the same in every install).

Measured (RTX 5070 Ti, build 229, 1280x720):

```
D3D12 3dgtest: 'Euro/Bnk/Vehicules/High/4x4.bnk' - 4x4.3dd + 4x4.3dg + 4x4.2dm, 7 material(s), 148 texture(s) uploaded
D3D12 3dgtest: material 'Paint' -> TD_CarMat_NM vs12/ps381 (0 switch(es) from the .2DM's); blend 2/1 op 1 cull 2
  bucket 0x2 depth 1/1, 3 of 6 texture(s) resolved; binds t0->s2(ibiza_offroad_occlu) t2->s4(ibiza_offroad_occlu)
  t3->s5(dirt_scratch) t4->s15; missing 0050414d45425543; 7 parameter(s)
D3D12 3dgtest: 39 node(s), 25 primitive(s): 25 draw(s) prepared, 0 skipped, 0 without a material, 0 without a
  program, 0 without a vertex layout, 0 refused by the device; 6 program(s), centre (-0.00 0.57 -0.00), radius 2.72
D3D12 3dgtest: 1280x720; 57442 px covered (6.23 %), 2 distinct colour(s), mean 219.6/219.6/219.6
D3D12 3dgtest: PASSED (the model reached the frame through its own material, pipeline and textures)
```

Three banks, all passing (one run each):

| bank | nodes | primitives | draws | programs | covered | colours |
|---|---|---|---|---|---|---|
| `4x4.bnk` (generic off-roader) | 39 | 25 | 25 | 6 | 6.23 % | 2 |
| `300_c_srt8.bnk` (Chrysler) | 37 | 22 | 22 | 6 | 4.76 % | 2 |
| `eph_l0c.bnk` (a beach playhouse) | 1 | 33 | 33 | 10 | 2.86 % | 4 |

**The frame is in the log as text**, because a coverage count cannot tell a car
from a smear: the probe prints a 64x24 brightness map of the last frame (one log
line per row, since a multi-line message is truncated). The 4x4, turned 35
degrees off the camera axis:

```
|                          @@@@@@@@@@@@@                         |
|                          @@@@@@@@@@                            |
|                         @@@@@@@@@@@-....*                      |
|                        @@@@@@@@@@@@-.....                      |
|                       @@@@@@@@@@@@@+::---                      |
|                        @@@@@@@@@@@@@@@@@                       |
```

- **SPIRV-Cross renumbers a stage's textures, so the SRV table must be filled by
  *register*.** A `.sob` pixel shader that samples D3D9 slots 2, 4, 5 and 15
  declares `t2_2d : register(t0)`, `t4_2d : register(t2)`, `t5_2d : register(t3)`,
  `t15_2d : register(t4)` - the variable names keep the slot, the registers are
  dense, and the root signature's table is indexed by the *register*. Filling it
  by slot number binds the wrong texture wherever a stage skips a slot: for the
  4x4 it bound slot 5's scratch map where the shader samples slot 8, and the same
  mistake on `eph_l0c`'s nine-slot materials painted the house's wood and grass
  white. `MapRegistersToSlots` (in the probe) reads the pairing out of
  `D3D12ShaderStage::HLSL` (`t8_2d : register(t5)` -> register 5 reads slot 8),
  and the material log prints the map (`binds t0->s2(...) t2->s4(...)`) so a wrong
  bind is visible rather than guessed at. A texture the stage declares as
  `TextureCube` (the `CUBEMAP` slot) is not in the map and binds the white 2D
  texture - the probe has no cube upload path yet.
- **The material handler runs before the permutation lookup, and it is not
  optional.** Looking the program up from the `.2DM`'s *raw* 48 switches got
  `TD_HardwareInstance` for the material called `Paint` with **11 switches
  differing** - the nearest permutation of the wrong family.
  `ApplyEdenMaterialHandler` (`render/eden/eden_material_handlers.h`, the
  original renderer's `FUN_00d71900`) is what rewrites those switches and, for a
  material exported with a family that has no library of its own, the GUID
  itself: `Paint` really is `TD_CarMat_NM`, and after the handler every material
  of all three banks matches its permutation exactly (0 differing switches).
- **A car's maps are not in its own bank.** `4x4.bnk` carries two `.2db` (glass,
  occlusion); its shared maps live in `Vehicules/common_car.bnk`. The probe
  uploads that bank too, under its own owner, and the material lookup's
  `findForOwner` fallback borrows from it - 148 textures instead of 2, and the
  slots that still miss are the runtime ones the *game* fills per frame (the
  `CUBEMAP` reflection slot, hash `0050414d45425543`). Spot banks are the
  opposite shape: `eph_l0c.bnk` ships 20+ `.2dm` (one per object), so the probe
  indexes *every* library in the bank by hashcode, the scene's own name first.
- **An empty texture slot must be bound to something.** `D3D12Scene::draw` only
  writes the descriptor table it is given, so a slot a shader samples but the
  draw did not fill would read whatever the table heap held there - garbage that
  can fault the GPU rather than paint a wrong colour. The probe always passes a
  full 16-entry table with the white texture in the unresolved slots, which is
  also the rule the Eden port will need.
- **A table per distinct texture set, not per draw - now implemented.** The
  table heap holds 4096 descriptors per frame slot (`D3D12Scene::kTableDescriptors`),
  and a world records thousands of draws, so the probe's first version (16
  descriptors copied per draw) could not fit a real scene's frame.
  `D3D12Scene::draw` now hashes the set's SRV handles, keeps a table index per
  *distinct* set for the frame slot, and clears the cache when the slot changes
  (its fence has retired the tables). Measured on the 4x4: **570 hits / 180
  misses over 30 frames = 6 resident tables** (one per material) instead of 750
  allocations, 96 descriptors per frame instead of 400 - and the frame is
  byte-identical (57422 px, mean 219.6), as is the scene test's PPM
  (`E505B37F...C3F778CEB`). `getTextureTableHits/Misses/Count` are public so a
  probe can print them.
- **The vertex layout comes from the pair, not from the shader alone.**
  `D3D12BuildVertexLayout` reads the vertex stage's dcl list and the primitive's
  own `.3DG` declaration; `MatBlack`'s primitive wanted four streams (the
  expanded SNORM normal plus the bone-index stream), and the layout's fallback
  stream carries a dcl the mesh does not have. 25 of 25 (22 of 22, 33 of 33)
  primitives created a PSO, 0 refused.
- **A near-flat palette is material data, not the draw path.** The generic cars
  draw in two tones because `TD_CarMat_NM`'s base colour comes from the
  instance's paint (the car catalogue), not from the bank: the shader samples
  only occlusion, scratch and glass, and its seven `.2DM` parameters are applied.
  The probe therefore *passes on coverage* (1 % of the frame) and prints the
  colour count beside it, with a warning when the frame is nearly flat - failing
  a correctly-drawn model because the game would have tinted it would hide the
  result.
- **OPEN - a diagnostic block changed the frame.** The probe's first version
  logged the dcls the mesh does not carry, by iterating `EdenProgram::pVS->Inputs`
  and `D3D12MeshData::Attributes` before `getPipeline`: **six consecutive runs
  drew an empty frame** (25 draws recorded, 0 px on both the screenshot and the
  readback, debug layer silent), and removing the block restored 57422 px in five
  runs. A single-line `OTDU_LOG_WARN` at the same place changes nothing, the
  logger formats into a 2048-byte buffer with `vsnprintf`, and the loop is
  read-only - so this is either an uninitialized read whose value the extra code
  perturbs or a GPU-side race the extra work masks. Parked on purpose: the Eden
  port must not add per-primitive logging or per-primitive work inside its draw
  build before a GPU capture (PIX / RenderDoc) explains it.
- **No regression**: `-d3d12-scenetest`'s frame is still
  `SHA256 E505B37F...C3F778CEB`, `-d3d12-meshconvtest` still passes, and
  `-renderer-selftest` still exits 0.

`-d3d12-nocull` is the A/B for a model whose winding does not match its cull mode
(it changes nothing on the three banks above, which is how that question was
closed).

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
| s0..s15 | sampler table | the D3D9 sampler set (14 entries, `render/sampler_heap.h`): a runtime descriptor array, not static samplers |

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
* **...and so are the varyings between the stages - but their *slots* are not the
  same number.** A vertex stage's k-th output must be the pixel stage's k-th
  input, which is only true when both declare the same list in the same order;
  DXSO's locations are D3D9's registers and may have gaps. `compilePair` reflects
  both signatures and pads the pixel input struct (section 3.9), so a caller that
  compiles a pair for an Eden program gets two stages the device will link.
* The HLSL text is kept per stage: `-d3d12-dump-shaders <dir>` writes it out, and
  a failed stage's HLSL is dumped too (section 3.5).

**The sampler heap exists** (2026-11-17, `-d3d12-samplertest`, section 3.6).
DXSO declares its samplers as one *runtime descriptor array*
(`SamplerState sampler_heap[] : register(s0, space0)`, `_SetupSamplerArray` in
`dxso_compiler.cpp`) indexed by a value the shader reads out of the push constant
block (`render_state_s0_s1_idx` ...), and D3D12 cannot index *static* samplers
dynamically. `D3D12Scene` therefore has **no static samplers at all**: its fourth
root parameter is a sampler descriptor table over s0..s15, and its heap
(`D3D12Scene::createSamplerHeap`) is filled once with the same fourteen entries as
the Vulkan `EdenRenderer::samplerHeap`, in the same order, from the same table -
`source/render/sampler_heap.h`, which both backends include. The per-material
choice is the *index* in the push constants (as it already was on Vulkan), so the
heap itself is never rewritten per draw.

`sampler_heap.h` is the part that keeps the two backends honest: the index values
are material data (`EdenMaterial::SamplerIndex`, written at push-constant byte 64)
and a table that drifted between the backends would sample one material's texels
with the other's addressing, silently. `-d3d12-samplertest` measures the D3D12
side of that contract; the Vulkan side is the sampler set the front end and the
world already draw with.

Then, per backend: bindings (root signature / CBV+SRV+sampler heaps vs
descriptor sets), the push-constant block (`kPushConstantsSize` = 128 B) becomes
root constants or a CBV, and the pipeline state (blend / cull / depth / stencil in
`PipelineDesc`) maps to `D3D12_GRAPHICS_PIPELINE_STATE_DESC`.

On D3D12 the binding side is now complete for everything an Eden draw needs
(section 3.7): the root signature has four CBVs per stage (b0..b3, filled from a
64 MB per-frame upload ring - `D3D12Scene::uploadConstants`), the b13 space1 root
constants, the t0..t15 texture table and the s0..s15 sampler table. The D3D9
material state (`D3DBLEND` / `D3DBLENDOP` / `D3DCMPFUNC` / `D3DCULL` /
`D3DCOLORWRITEENABLE`) passes through `PipelineDesc::D3D9State` untranslated; the
blend factor is `OMSetBlendFactor` (dynamic state) and the alpha test is DXSO's
own `discard`.

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
   (section 4). Same day, step 1c: **the sampler heap** (`-d3d12-samplertest`,
   section 3.6) - a shader-visible sampler descriptor heap in the root
   signature's last parameter, the same fourteen entries as the Vulkan sampler
   set (`render/sampler_heap.h`), so a shader can index the D3D9 sampler set the
   way DXSO emits it. Same day, step 1d: **the constant buffers and the D3D9
   material state** (`-d3d12-constest`, section 3.7) - b0..b3 per stage from a
   per-frame upload ring, and `EdenMaterial`'s raw D3D9 state mapped into the PSO
   (blend factors / op, write mask, depth func, D3DCULL). Measured exact: left
   half 128/0/127 (0.5 red + 0.5 blue), right half 0/127/0 (0.5 green), on the
   RTX 5070 Ti and on WARP. Step 2a, 2026-11-18: **an original shader draws**
   (`-d3d12-edentest`, section 3.8) - a real `.sob` permutation (`TD_Lamp
   vs5/ps1`), its constant file written by the *shared* filler
   (`render/eden/eden_constants.h`, extracted from `EdenRenderer`), the engine's
   checker in t0 and 110224 px (11.96 %) of lit checker on the frame, identical
   on WARP. Step 2a2, 2026-11-18: **the vertex/pixel interface** (section 3.9) -
   the refused pair was a VS->PS *slot* mismatch (D3D9's sparse varying registers
   against HLSL's dense ones), the debug layer had been reporting it into a filter
   that dropped every error, and `D3D12ShaderCompiler::compilePair` now pads the
   pixel stage's input struct to the vertex stage's outputs. `TD_LightBeam
   vs2/ps1` binds and draws (110224 px, the same exact quad), `TD_Lamp`'s numbers
   are unchanged, and every other probe is byte-identical. Step 2a3, 2026-11-18:
   **the catalogue-wide pipeline tally** (`-d3d12-pipetest`, section 3.10) - every
   library's VS/PS pair from its hashcodes, `compilePair`, a real
   `CreateGraphicsPipelineState` for each, with the PSO cache batched. It found
   two backend defects (the pixel stage's SV_Position register drifting when it
   stops early, and a pipeline key that identified stages by address); both are
   fixed and the refusals they caused are gone. Step 2b1, 2026-11-18: **a real
   model draws** (`-d3d12-3dgtest`, section 3.13) - the bank's own `.3DD` scene,
   `.3DG` streams and `.2DM` materials, every node with its transform, 25 of 25
   primitives of the 4x4 (22 of 22 of the SRT8) through a real PSO, and the three
   rules the Eden port needs measured rather than assumed: the material handler
   runs *before* the permutation lookup (`Paint` is `TD_CarMat_NM`, not
   `TD_HardwareInstance`), a car's textures live in a sibling bank the lookup has
   to borrow from, and an unresolved slot must bind the white texture instead of
   an unfilled descriptor. Step 2b2, 2026-11-18: **the Eden draw path as a
   module** (`D3D12Eden`, section 3.14) - program pair, mesh upload, pipeline
   cache, the shared constant filler, the register-mapped texture table and the
   recorded draw in one class, its parameters exactly `EdenRenderer::draw`'s;
   `-d3d12-3dgtest` now goes through it (its three banks draw the identical
   pixels), and `-instances N` measured the question 3.13 left open - a draw
   costs 9,216 ring bytes, so the 64 MB ring overflowed at 7,281 draws and
   is now 256 MB (~29,000 draws, a main pass plus its Z pre-pass). Still open in
   3.15: `EdenModel`'s mesh upload, offscreen render targets, and a parallel
   record (~2 us per draw single-threaded). Step 2b, in progress: **the Eden
   scene itself** - `D3D12Scene` now carries multi-stream meshes (section 3.11)
   and the vertex layout from a `.3DG` declaration plus the shader's dcl list,
   which is the interface `EdenRenderer` needs:
   `EdenRenderer`/`EdenWorld`/`EdenSceneView` pointed at `D3D12Scene` (the
   `EdenProgram` -> pipeline cache via `compilePair`, `FillEdenConstants` into the
   b0 CBV and the b13 push constants, the sampler heap and texture table it
   already binds), then
   the render targets / shadow / reflection / post passes, and then the 3D game
   modes (still refused with one log line).
5. **D3D11**: the same translated HLSL, a thin device; it is the cheap one once (4) exists.
6. **D3D9**: the original bytecode path (a near pass-through), with SM 3.0 limits documented
   where the engine's own shaders cannot follow.

Next step, precisely: **where the bytes come from** - `source/render/eden/eden_model.cpp`
and the D3D12 mesh source. `D3D12Eden` already answers the rest of a draw
(program -> pipeline cache, `FillEdenConstants` into b0, the register-mapped
texture table, the recorded command: section 3.14), so the Eden port is now
`EdenMesh` carrying D3D12 buffers: a model packs its primitives into two buffers
and hands each item a (buffer, offset, count), which needs either
`D3D12Scene::Mesh` to take a base vertex / index offset (`BaseVertexLocation` /
`StartIndexLocation`) or one upload per item. The questions that came before it
are all answered: the shaders compile (3.5), the sampler heap they index draws
with the right state (3.6), the constant buffers and the D3D9 material state
arrive (3.7), one real permutation draws end to end with the renderer's own
constants (3.8), the stages link for a shader that skips a varying register
(3.9), every distinct program in the game creates a PSO (3.10), a whole real
model draws (3.13), and a frame of 25,600 draws has its ring, table-cache and
CPU cost measured (3.14). After the mesh source: offscreen targets, then a
parallel record (3.15).

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
- **DXSO's sampler is a runtime descriptor array, so the scene has no static
  samplers.** `SamplerState sampler_heap[] : register(s0, space0)` indexed by a
  value from the push constant block (`render_state_s0_s1_idx` ...) is the D3D9
  sampler set, and D3D12 cannot index *static* samplers dynamically. The fix is a
  sampler descriptor table (s0..s15) plus a shader-visible sampler heap
  (section 4); a static sampler and a descriptor range cannot share a register, so
  the old static set had to go with it. `-d3d12-samplertest` (section 3.6) is the
  proof that the index reaches the sampler.
- **`SetDescriptorHeaps` replaces the whole set, per command list.** The scene
  binds its SRV table heap *and* its sampler heap on every draw
  (`D3D12Scene::draw`), because the 2D layer (`D3D12UI`) and ImGui each call
  `SetDescriptorHeaps` with their own heap in between - a table is only valid
  while its heap is in the current set, and a stale table is error #646 at draw
  time, not at the `SetGraphicsRootDescriptorTable` call. One heap of each type
  may be bound at once, so the scene's two are exactly SRV + SAMPLER.
- **The front face is decided in screen space, where Y points down.** A Vulkan
  pipeline with `frontFace = counter-clockwise` maps to
  `D3D12_RASTERIZER_DESC::FrontCounterClockwise = FALSE`. D3D9 spells its
  culling the other way round (`D3DCULL_CW` 2 culls the *front* faces of a
  clockwise-front mesh), so `PipelineDesc::D3D9State` maps 4 -> `CULL_BACK`,
  2 -> `CULL_FRONT`, 1 / 3 -> `CULL_NONE`, and swaps 2 and 4 for a mirroring
  view - the exact rules `EdenRenderer::getPipeline` applies to Vulkan, mirrored
  rather than translated. The scene test culls nothing on purpose.
- **A root CBV is a GPU virtual address, not bytes.** Per-draw constants have to
  be copied into a host-visible buffer before the draw - `D3D12Scene` keeps a
  256 MB mapped UPLOAD ring per frame slot (`uploadConstants`), 256-byte aligned
  because that is the alignment a root CBV requires. Passing a pointer to a
  stack block (the Vulkan habit with `vkCmdPushConstants`) is not an option here,
  and a draw does not know when the GPU reads a block, so the ring is what makes
  the *next* draw's overwrite harmless.
- **An Eden draw costs 9,216 bytes of that ring, and the number cannot be
  reduced by filling less.** The VS constant file is 4,368 bytes (16 int4
  registers, 256 float4, the bool bitmask), the PS one 3,856, the spec block is
  64 and every block rounds up to 256, so the ring holds 9,216 per draw
  (8,224 is what is copied). A shader's `cbuffer_t` declares those offsets, with
  the bitmask at the *end*, so shrinking the block to the registers a program
  names would move it. The ring therefore bounds the frame's draw count (64 MB
  held 7,281 draws and a 12,800-draw frame needed 176 % of it; it is 256 MB now,
  ~29,000 draws - section 3.14 has the measurements). A frame that overflows
  still *draws*: the extra draws read the zero block, which is why
  `D3D12Scene::getFrameConstantBytes()` exists and why the probe prints the
  percentage.
- **D3D12's blend factor and alpha test are not pipeline state.** `SRC_ALPHA` /
  `INV_SRC_ALPHA` are, but `D3DBLENDFACTOR` / `D3DBLEND_INVBLENDFACTOR` read the
  *dynamic* constant - `OMSetBlendFactor` per draw, deliberately out of the
  pipeline cache key - and D3D9's alpha test is already a `discard` in the DXSO
  shader (spec constant block + the push constant alpha reference), so a PSO
  alpha-test field would be dead state that disagrees with the shader.
- **SPIRV-Cross reports cbuffer *instance* names, not the HLSL type names.**
  `compiler.get_name( resource.id )` is `c`, `spec_state`, `clip_info`,
  `D3D9SharedPS` for a DXSO stage, while the HLSL says `cbuffer_t`,
  `spec_state_t`, `clip_info_t`. `D3D12ShaderStage::ConstantBufferNames` keeps
  what was actually seen, and the Eden side binds by *shape* rather than by
  string (`-d3d12-edentest`): a block naming the specialization state gets the
  spec dwords, clip/shared get the zero block, anything else is the constant
  file. A strict `== "cbuffer_t"` match silently binds nothing - the measured
  symptom is a quad that collapses to a point (zero view-projection) and a frame
  with 0 covered pixels.
- **`D3D12_MESSAGE_SEVERITY` counts *up* from CORRUPTION.** CORRUPTION = 0,
  ERROR = 1, WARNING = 2, INFO = 3, MESSAGE = 4. A filter written as "skip what
  is below WARNING" therefore drops **every error** - which is exactly how
  `TD_LightBeam vs2`'s refused pipeline looked like an `E_INVALIDARG` with *no*
  debug message for a whole session (section 3.9). `flushDebugMessages` now logs
  CORRUPTION/ERROR as errors, WARNING as a warning and the rest only with
  `-verbose`, and it remembers the descriptions it has already printed (the
  per-frame flush otherwise repeats one misuse once a frame). Corollary: "the
  debug layer is silent" is a claim to re-check after touching that filter.
- **A pixel stage may stop early, but its SV_Position may not.** D3D12 links the
  stages by *hardware register*, and a system value takes the register after the
  generics - SPIRV-Cross emits `gl_Position : SV_Position` last in the vertex
  stage's output struct. A vertex stage with 11 varyings therefore has
  SV_Position in register 11, and a pixel stage that reads only 9 puts its own in
  register 9: the pair is refused with *"Semantic 'SV_Position' is defined for
  mismatched hardware registers"* although every generic semantic matches, and
  the log looks aligned because reflection reports *slots*, not registers. Pad
  the pixel input struct to the vertex stage's length (sections 3.9, 3.10).
  Corollary: an empty pixel interface is not a failed reflection - `TD_Bones`'
  flat-colour pixel shaders are exactly that - and a pixel stage that takes no
  parameter at all needs no padding because it has no register to align.

- **D3D12 links the vertex and pixel stages by varying *slot*, and HLSL slots are
  dense.** The k-th non-system-value output is compared with the k-th input;
  HLSL hands out those slots in declaration order, while DXSO's locations are
  D3D9's varying *registers*, which a `.sob` shader may skip (`TD_LightBeam`'s VS
  writes oT0..oT4 and oT7, its PS reads t0,t1,t2,t7). The pipeline is then
  refused with *"Semantic 'TEXCOORD' is defined for mismatched hardware
  registers between the output stage and input stage"* - and the stage that
  "looks wrong" depends on the pairing, so blaming one bytecode is a trap: with a
  three-line pixel shader the pair links or not for reasons that have nothing to
  do with that shader. `compilePair` (section 3.9) pads the pixel input struct.
  Three FXC facts that fix rests on, each measured with `D3DCompile` +
  `D3DDisassemble`:
  - an input declared but never read **stays** in the input signature with an
    empty `Used` mask (so a pad is real state, not a comment);
  - a `float` pad is **packed** with its neighbour (`float` TEXCOORD3 and `float`
    TEXCOORD4 both landed in register 3, components x and y), so pads must be
    `float4` - one whole register each;
  - a system value declared first **takes** the slot (`SV_Position` then
    `TEXCOORD0` -> registers 0 and 1), so varyings are emitted before system
    values.
  Do not assume the tally (3.5) means every stage can be *bound*: it measures
  compilation, not linkage.
- **A compiled stage is not necessarily a pipeline D3D12 accepts** - and the
  reason may be the *pair*, not the stage. `TD_LightBeam vs2/ps1` compiles to
  valid SM 5.1 DXBC (section 3.5) and failed `CreateGraphicsPipelineState` for a
  VS->PS slot mismatch (section 3.9); `-d3d12-edentest` logs a refused pair and
  moves to the next candidate, and `getPipeline`'s failure line prints both
  varying lists (`D3D12ShaderCompiler::describeVaryings`).
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


