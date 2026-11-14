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
