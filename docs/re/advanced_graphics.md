# Advanced graphics page and the upscaler road map

Not a retail feature: OpenTDU2's own page (`source/game/advanced_graphics.{h,cpp}`), because the retail
option pages are Flash movies whose rows are fixed in their AVM1 code [DATA]. It is native (Dear ImGui)
and opens from the **Video** page with **Y / Triangle** (`MENU_OPTION_2`) or anywhere with **F2**.
Up / Down = row, Left / Right = change, OK / Back = close and save (`options.ini`).
The profiles and the measured cost of each row are in **`docs/re/quality_profiles.md`** (session 73).

## What exists [INF: written for this project]
| Row | Where it acts |
|---|---|
| **Quality profile** (Custom / High / Medium / Low / 60 fps / Steam Deck / Legion Go) | `ApplyQualityProfile()` (`game_options.cpp`): sets every row below, the window size (handheld profiles) and the fps cap; `-profile <name>` for a run |
| Render scale 50-200 % | `EdenSceneView::ensureTargets` sizes the scene / viewpos / final targets to `backbuffer * scale` |
| Upscaling filter (bilinear, Catmull-Rom bicubic) | `resources/shaders/upscale.frag`, the last scene pass (`EdenSceneView::draw`) |
| Sharpening 0-10 | same shader, contrast adaptive (own implementation, **not** AMD CAS) |
| Edge smoothing | same shader, FXAA-like edge blend at render resolution (own implementation, modest quality) |
| Color grading, haze, sun flare, sun shadows | runtime versions of `-nophotoshop`, `-nohaze`, `-nosunflare`, `-noshadows` |
| Shadow distance 50-150 % | `EdenShadows::renderCasters` scales `SET_SHADOW_CONFIG_MAX_CAST_DIST`; **no measurable effect at the FREERIDE spawn** (docs/re/quality_profiles.md §4) |
| HUD detail | `FreeRideHud::draw`: full = HUD / GAUGES Flash movies, simple = minimap + built-in gauge |
| Water reflections | `EdenSceneView::renderOffscreen`: 0 = render once, 1 = every frame, N = every Nth |
| Draw distance 50-150 % | `EdenWorld::drawInstances`: scales the `.pmi` instance distance (the strongest draw-call lever) |
| Frame rate limit | `TestDriveGameInstance::mainLoop` |
| Ray tracing | the ray tracing extension chain requested when the device is created (`RenderDevice::setRayTracingEnabled`); `options.ini` `ray_tracing` |
| Ray traced world | acceleration structures for the world's meshes (`IsRayTracingWorldEnabled`); `options.ini` `ray_tracing_world` |
| Run ray tracing self test | an **action** row (Right runs it, Left does nothing): `RayTracing::runSelfTest`, verdict shown on the row and in the log |

At 100 % scale with everything off the old `blit` shader is used, so the default frame is unchanged.

**The two ray tracing rows are restart settings**, and the page says so rather than pretending:
the Vulkan device is created once and the structures are built as models load, so the page shows
what is *actually* live (`Ray tracing: active (VK_KHR_ray_query)`, `World structures: on / off`)
and adds `Saved setting not in effect yet: restart to apply` when the saved value differs.
`-nort` and `-rtworld` still override the ini, so a headless A/B run never has to edit it.
The ray tracing side is specified in `docs/re/ray_tracing.md`.

## Finding: render scale does not speed the game up yet
`-fixedtimestep`, Ibiza opening scene: 100 % 52.1 ms/frame, 50 % 50.8 ms, 150 % 50.3 ms. The frame is
**not pixel bound** (CPU / draw submission, STATE priority 6). Any upscaler (FSR, DLSS) only buys GPU
time, so it will not raise the frame rate until the draw cost is fixed. Measure first (`[perf]` lines).

## FSR / DLSS: what each one needs
- **FSR 1.0 (EASU + RCAS), spatial.** Needs only the colour image: a drop-in for the upscaler slot
  (`GameOptions::Upscaler = 2`), run before the UI (the HUD is drawn afterwards at native resolution).
  AMD's `ffx_a.h` / `ffx_fsr1.h` (GPUOpen-Effects/FidelityFX-FSR, MIT) are GLSL-compatible and go through
  `tools/compile_shaders.py`. **Done 2026-11-14:** vendored from FidelityFX-SDK as `third_party/fidelityfx-fsr1` (HLSL path, dxc), `GameOptions::Upscaler = 2`.
- **FSR 2 / 3 and DLSS (temporal).** Need per-pixel **motion vectors**, depth (we have it), a **sub-pixel
  camera jitter** in the projection, exposure and a reactive mask. The engine has none of the first two:
  the Eden shaders are the original DXSO translated, with no velocity output. Steps: (1) jitter the
  projection (`EdenSceneParams`), (2) a camera-motion velocity pass from depth + previous view-projection
  (covers static world), (3) per-object previous transforms for cars / skinned meshes, (4) the SDK glue.
- **DLSS** is NVIDIA's proprietary NGX / Streamline SDK: needs an RTX GPU and its licence forbids bundling
  it in an open repo with no licence of its own. Realistic form: an optional runtime plugin that loads
  the user's own `nvngx_dlss.dll` when present. Same engine prerequisites as FSR 2.
- Order of work: perf (draw cost) -> FSR 1.0 -> jitter + velocity -> FSR 2 / 3 -> DLSS plugin.

## Candidate settings not done
SSAO / GTAO (needs only depth + normals; the depth is there, normals would come from depth), shadow
resolution and cascade count live (`EdenShadows::Config::Resolution` is fixed at init), SMAA / TAA (TAA
shares the jitter + velocity work), motion blur, view distance split from the Environment row.


## FSR 3 (temporal) - the optional plugin (2026-11-14)
`GameOptions::Upscaler = 3` ("AMD FSR 3 (temporal)"). Built on the FidelityFX **API** (`ffx_api`), loaded at run time:
- **DLLs (not in git, binary-only AMD licence, `third_party/fidelityfx-api/LICENSE.md`):** copy `amd_fidelityfx_loader_vk.dll` and
  `amd_fidelityfx_upscaler_vk.dll` from the FidelityFX SDK (`Kits/FidelityFX/signedbin`) next to `opentdu2*.exe`, or into
  `third_party/fidelityfx-api/bin/` (gitignored, found from `build*/bin`). Without them the option logs one warning and the final pass
  falls back to a plain bilinear upscale.
- Code: `source/render/upscalers/fsr3_upscaler.{h,cpp}` (context + dispatch), `EdenSceneView::runFsr3` (inputs: the final colour at
  render size, an R32F copy of the depth - `depthcopy.frag`, the velocity buffer, the Halton jitter), run after `runVelocity`.
- The device now enables `shaderInt16` and `VK_KHR_compute_shader_derivatives` (derivative group linear) when the GPU has them; the
  FSR 3 compute shaders need both (validation named the two missing capabilities).
- Conventions (proved by the reprojection test, see the session entry): velocity = previous uv - current uv, so `motionVectorScale` =
  render size; jitter in pixels with the SDK's sign (NDC x += 2jx/W, y -= 2jy/H); input colour is LDR (tone mapped) so the context
  uses `NON_LINEAR_COLORSPACE` + auto exposure. Sharpening slider 1..10 -> FSR's own sharpening 0.1..1.


## Intel XeSS (temporal) - optional plugin (2026-11-14)
`GameOptions::Upscaler = 4` ("Intel XeSS (temporal)"). Unlike FSR 3 it needs the **device** to be made with XeSS's requirements, so
choosing it takes effect after a **restart** (the saved `upscaler = 4` is read before the Vulkan instance exists; `-xess` forces it).
- **Not in git:** Intel's headers and `libxess.dll` are not redistributable from this repo (`third_party/xess-sdk/LICENSE.txt`).
  Unpack the XeSS SDK (github.com/intel/xess, v3.0.2 tested) so that `third_party/xess-sdk/inc/xess/xess_vk.h` and
  `third_party/xess-sdk/bin/libxess.dll` exist (the DLL may also sit next to the exe). Without the headers
  `source/render/upscalers/xess_upscaler.cpp` compiles to a stub; without the DLL the option logs one warning and plain-upscales.
- What XeSS asked for on this machine (RTX-class driver, Vulkan 1.4 loader): Vulkan >= 1.3, no instance extensions, device extension
  `VK_EXT_mutable_descriptor_type` and a chain of core 1.1 / 1.2 / 1.3 feature structs. Because those are core structs, the engine's
  own `VK_EXT_descriptor_indexing` / `shader_demote` / `buffer_device_address` structs are folded into them (a chain may carry only one
  of the two); validation names that rule. If the device refuses the combination it is created again the plain way.
- Conventions: low-res jitter-free motion vectors in pixels (`xessSetVelocityScale` = render size; ours are uv units), jitter as applied to
  the projection (the `-xessjitterflip` A/B is measurably blurrier on a static scene: Laplacian variance 38 vs 61), LDR input + auto
  exposure, depth as the R32F copy shared with FSR 3. The preset is the one whose own input size is closest to the render size
  (XeSS 2's Quality is 1.7, Ultra Quality 1.3).


## NVIDIA DLSS (temporal) - optional NGX plugin (2026-11-14)
`GameOptions::Upscaler = 5` ("NVIDIA DLSS (temporal)"), RTX GPUs only, **restart to apply** (NGX names Vulkan instance / device extensions before
they are created; the saved `upscaler = 5` or `-dlss` is read at start-up).
- **Not in git** (NVIDIA's licence, `third_party/dlss-sdk/LICENSE.txt`): put the NGX SDK files in `third_party/dlss-sdk/` as
  `include/nvsdk_ngx*.h`, `lib/nvsdk_ngx_d.lib` (static) and `bin/nvngx_dlss.dll` (all from github.com/NVIDIA/DLSS,
  `lib/Windows_x86_64/x64` and `.../rel`). CMake then defines `OTDU_HAVE_DLSS`, adds the include dir and links the lib; without them
  `source/render/upscalers/dlss_upscaler.cpp` is a stub. The DLL is found next to the exe or in `third_party/dlss-sdk/bin`.
- NGX asked for (RTX 5070 Ti, driver 621): device extensions `VK_NVX_image_view_handle`, `VK_NVX_binary_import`, `VK_KHR_push_descriptor`, no
  instance extensions. Also needed in the engine: `shaderStorageImageWrite/ReadWithoutFormat` (the DLSS shaders declare them) and
  `TRANSFER_DST` on the render target images (NGX clears images it is given).
- Init is `NVSDK_NGX_VULKAN_Init_with_ProjectID` (custom engine, a fixed GUID; no NVIDIA application id). Preset = the one whose optimal input
  is closest to the render size. Flags: low-res motion vectors, auto exposure, LDR. Vectors scaled by the render size, jitter as applied
  to the projection (`-dlssjitterflip` is a control: Laplacian variance 115 vs 109 on a static scene, DLSS is much less sensitive to it than XeSS).
