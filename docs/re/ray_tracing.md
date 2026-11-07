# Ray tracing: what is possible here, and what is not

Not a retail feature. This is OpenTDU2's own analysis of whether the renderer
can carry a ray traced pass, and the record of the ones that exist.
**Status: steps 3 and 6 — the world's meshes are in the acceleration structures,
rays are traced against them, and the first consumer exists: `-rtao <samples>`
(screen space ray traced ambient occlusion, the step 6 pass, done before step 4
because it needs no terrain in the structures to be usable).** RT sun shadows
(step 4) is still the pass that would *replace* CPU work.

## 1. What exists [INF: written for this project]

| Piece | Where |
|---|---|
| Device capability query + extension chain | `source/render/vulkan/render_device.cpp`, `createDevice()` |
| `RenderDevice::supportsRayTracing()` | `source/render/vulkan/render_device.h` |
| `-nort` (force the device back to no ray tracing, for A/B) | same file, `createDevice()` |
| Log line `Ray tracing: available / disabled (-nort) / not supported` | `render_device.cpp` |
| Read-only readout on the Advanced graphics page | `source/game/advanced_graphics.cpp` |
| Buffer device address, AS storage / scratch / readback buffers, one time submits | `source/render/vulkan/render_device.{h,cpp}` |
| `RayTracing`: BLAS / TLAS build, the ray query probe, the self test | `source/render/rt/ray_tracing.{h,cpp}` |
| The ray query compute shader | `resources/shaders/rt_probe.comp` -> `builtin/rt_probe_comp.h` |
| `-rttest` (the self test) | `source/render/gs_render.cpp`, `createDevice()` |
| The two settings rows + the self test action on the F2 page | `source/game/advanced_graphics.cpp` |
| `ray_tracing` / `ray_tracing_world` in `options.ini` | `source/game/game_options.{h,cpp}` |
| `EdenMesh::BlasAddress` (0: none) | `source/render/eden/eden_renderer.h` |
| One BLAS per mesh at model load | `source/render/eden/eden_model.cpp` |
| The drawn instances of the frame | `EdenRenderer::RtInstance`, captured in `recordDraw` |
| The frame TLAS + the camera probes | `GSRender::updateRayTracing()`, `-rtworld` |

`-nort` exists so an A/B run never needs a rebuild, and so a driver that
advertises the extensions but not the features can be ruled out in one run.

### The settings (F2, or Options > Video with Y / Triangle)

| Row | `options.ini` | Effect |
|---|---|---|
| Ray tracing | `ray_tracing` (default 1) | whether the ray tracing extension chain is requested when the Vulkan device is created |
| Ray traced world | `ray_tracing_world` (default 0) | whether the world's meshes get acceleration structures |
| Run ray tracing self test | — | an action: Right runs it, the verdict appears on the row and in the log |

Both settings are **restart settings** and the page says so: the device is created once and the
structures are built as models load, so the page reports what is actually live (`Ray tracing:
active (VK_KHR_ray_query)`, `World structures: on / off`) and adds `Saved setting not in effect
yet: restart to apply` when the saved value differs. `-nort` and `-rtworld` override the ini, so a
headless A/B run never has to edit it.

All five combinations were verified (ini on/off × no flag / `-nort` / `-rtworld`): the device is
created without ray tracing when the option is off (and then no structures are built even if the
world option is on), and the flags win over the ini in both directions.

## 2. Why the extension chain is exactly this [CODE: Vulkan spec dependencies]

The application asks for **Vulkan 1.1** (`render_device.cpp`, `appInfo.apiVersion`),
so everything that is core in 1.2 has to come in through its KHR alias:

| Extension | Needs | Why it is here |
|---|---|---|
| `VK_KHR_acceleration_structure` | Vulkan 1.1 + `VK_EXT_descriptor_indexing` + `VK_KHR_buffer_device_address` | BLAS / TLAS |
| `VK_KHR_ray_query` | `VK_KHR_acceleration_structure` + (`VK_KHR_spirv_1_4` or Vulkan 1.2) | rays from a normal shader |
| `VK_KHR_spirv_1_4` | Vulkan 1.1 + `VK_KHR_shader_float_controls` | SPIR-V 1.4 modules |
| `VK_KHR_buffer_device_address` | Vulkan 1.1 | AS memory is addressed, not bound |
| `VK_KHR_deferred_host_operations` | — (an *interaction*, not a requirement) | threaded AS builds, enabled when present |
| `VK_EXT_descriptor_indexing` | already enabled for the DXSO shaders | required by the AS extension |

The trap worth remembering: **`VK_KHR_spirv_1_4` requires
`VK_KHR_shader_float_controls`**, which is easy to miss because the failure only
shows up as a `vkCreateDevice` failure or a validation VUID, never as a compile
error. Every extension is added only when the driver reports it, and the whole
group is skipped unless *all* of it is present.

Feature structures enabled: `VkPhysicalDeviceBufferDeviceAddressFeaturesKHR`
(`bufferDeviceAddress`), `VkPhysicalDeviceAccelerationStructureFeaturesKHR`
(`accelerationStructure`), `VkPhysicalDeviceRayQueryFeaturesKHR` (`rayQuery`).
They are chained after the DXSO ones through a small head/tail
`VkBaseOutStructure` chain builder, so one `createInfo.pNext` still serves every
case.

## 3. What the renderer already gives a ray traced pass

- **Depth, full resolution, already sampled.** `pSceneTarget->Depth` feeds
  `gViewPos` (`FullscreenPass` + `viewpos.frag`), the haze, the projected shadow
  boxes and the sun shadow resolve (`eden_scene_view.cpp`). A ray origin per
  pixel is therefore nearly free.
- **The terrain heightfield is NOT usable as triangles** (found in step 3, and
  the biggest remaining item). The level mesh (`eden_world.cpp`, `loadHeightfield`)
  keeps **one R32 height per vertex** and the shader rebuilds the positions from
  the grid coordinates, so there is no position stream for a BLAS to read
  (`VAF_POSITION_R32_SFLOAT` -> `GetBlasPositionFormat` returns
  `VK_FORMAT_UNDEFINED` and the mesh is skipped). The terrain is therefore absent
  from the TLAS and a downward ray hits only what a *model* provides. RT shadows
  over the terrain need either a generated XYZ position buffer (the heights are
  already there) or per-cell AABBs (`VK_GEOMETRY_TYPE_AABBS_KHR`).
- **Model meshes are all fine.** In the Ibiza sector test every model mesh got a
  BLAS with nothing skipped (`library.3dd`: 1539 of 1539), so the two position
  formats the models use (`R32G32B32_SFLOAT`, `R16G16B16A16_SFLOAT`) both work.
- **Static geometry is upload-once.** An `EdenMesh` holds raw D3D9 vertex /
  index streams built once per model and re-used by every pass, so **one BLAS
  per mesh, built once** — no per-frame rebuild.
- **Instance transforms are CPU side.** `EdenWorld::drawInstances` and
  `EdenRenderer::deferredDraws` already carry `Matrix4x4f World` per draw, so the
  per-frame TLAS input is a list that already exists.
- **The consumers can be swapped without touching the original shaders.**
  `pShadowRealTimeOverride` and `material.ProjectiveSlot` / `pProjectiveTexture`
  let the engine bind *a different texture* to the slots the original
  `TD_Shadow*` / `TD_Water` / `TD_Furnitures` materials sample. A ray traced
  shadow mask or reflection buffer can therefore be fed to unmodified Eden
  shaders.

## 4. What blocks it

- **DXSO cannot emit rays.** The project's identity is the original TDU2 D3D9
  shaders translated to SPIR-V. D3D9 SM2/SM3 bytecode has no ray queries and
  DXVK's front-end will never produce them, so **every ray traced pass must be
  new hand-written GLSL** in `resources/shaders/`, compiled by
  `tools/compile_shaders.py` — which is pinned to `--target-env vulkan1.1` and
  will need SPIR-V 1.4 for a ray query module.
- **No G-buffer.** No normals, no albedo, no material buffer: the pass is
  forward with the original per-material blend / depth / stencil states. Normals
  would have to come from depth (as `advanced_graphics.md` already notes).
- **Skinned meshes are the real blocker.** Avatars and cars skin *in the vertex
  shader*, so the CPU never sees posed vertices — a BLAS cannot be built from
  the unposed mesh. And avatars dominate the frame (`-nopnj` 86 -> 24 ms).
- **macOS / Linux must fall back.** MoltenVK has no ray tracing at all, so the
  gate has to stay optional and the current path has to remain frame-identical.

## 5. The one argument in favour: this frame is CPU bound

The GPU is idle, and the passes RT would replace are expensive *on the CPU*
(`docs/STATE.md`, perf session):

    88.6 ms default  ->  66.9 with -noshadows  ->  60.7 with -noprojectors  ->  31.7 with all three

`-noshadows` alone is ~22 ms of CPU. Because the frame is not pixel bound
(52.1 / 50.8 / 50.3 ms at 100 / 50 / 150 % render scale), a GPU ray traced
shadow pass is not a cost here — it is a candidate **win**, and it is the only
part of this document with a measured reason to exist. The cost side of the
ledger is the per-frame TLAS build, which lands on the bottleneck (CPU) and is
why deferred host operations are enabled.

## 6. Order of work

1. **Device gate** — done.
2. **Acceleration structures** — done: one BLAS per triangle range (a sub range
   of a shared vertex / index buffer, which is how `EdenModel` stores its
   primitives), a TLAS from an instance list, both built on the GPU through a
   one time submit (`RenderDevice::executeImmediate`). `RayTracing` also has the
   ray query compute probe that proves them.
   Note: the builds are GPU side one time submits. `VK_KHR_deferred_host_operations`
   is enabled but unused: it is only needed if the builds ever move to a CPU
   thread, which is the point at which the per-frame TLAS cost on the CPU
   matters (see §5).
3. **The world's meshes in the structures** — done (`-rtworld`): a BLAS per mesh
   at model load, a TLAS per frame from the instances the scene recorded, and
   three probe rays from the camera. The terrain heightfield is *not* included
   (see §3) — that is the first thing step 4 has to solve.
4. **RT sun shadows** — the only step with a measured reason to exist. A
   `rt_shadow.comp` ray query writing a visibility mask, bound through the
   existing `setShadowRealTimeTexture` override. A/B against `-noshadows`. It
   needs the terrain (see §3) or its shadows will be wrong wherever the ground
   should be the occluder.
5. **RT reflections** — `rt_reflection.comp` feeding `ProjectiveSlot`, and the
   ocean mirror camera.
6. **RT ambient occlusion** — **done (session 50, `-rtao <samples>`)**, done
   before step 4 because it needs no terrain in the structures to be usable.

### Step 6: `-rtao <samples>` — ray traced ambient occlusion

`resources/shaders/rt_ao.comp` (ray query compute, 8x8 tiles) reconstructs the
world position per pixel from the scene depth (the `viewpos.frag` law + the camera
basis pushed as constants), derives the normal from two neighbour taps, and casts
`<samples>` cosine-weighted hemisphere rays against the frame TLAS (any hit within
3 m = occluded). The R8 mask lands in an `rtAO` render target written as a storage
image and is multiplied onto the scene by `rt_ao_combine.frag` in place of
`blit.frag` — the plain-blit path only, the upscaler path does not sample it.
`-showaomask` views the raw mask. Requires `-rtworld` (the TLAS; one frame stale —
it rebuilds at endFrame). The dispatch records inside the frame command buffer,
bracketed as the "rtao" pass of `[perf] gpu`. **Default off; `-rtao 0` disables it
entirely and no target is created.**

**Costs, measured** (FREERIDE, the `-tp 21796.3,24188.1` spawn, 1280x720, RTX
5070 Ti, validation clean):

| `-rtao` | rtao GPU | whole rest of the frame GPU |
|---|---|---|
| off | — | ~2.4 ms |
| 4 | 0.46 ms | ~2.4 ms |
| 16 | 1.82 ms | ~2.4 ms |
| 64 | 7.57 ms | ~2.4 ms |
| 256 | 30.14 ms | ~2.4 ms |

Linear at ~0.12 ms per ray per pixel (~8 Grays/s at 256 rays). **The GPU stops
being idle at roughly 220 rays per pixel** on this scene — that is where the RT
cost crosses the ~35 ms CPU frame. The owner's "stress the GPU" question
(2026-10-30): answered — 16 rays doubles the GPU work, 64 quadruples it, and the
CPU frame is untouched by the ray count (one dispatch call).

Known edges, all observed not guessed:
- **The terrain is not in the TLAS** (§3), so ground contact gets less occlusion
  than it should; ground pixels only occlude against model meshes.
- **Under dense tree canopy the road goes near-black**: physically consistent
  (the canopy is a dense occluder in the TLAS) but heavy; visual tuning (distance
  falloff, a strength clamp like `mix(1, ao, 0.6)`) is polish, not done.
- Run-to-run AO at one pixel can differ a lot between runs — the TLAS holds what
  streaming had loaded, which varies.
- The `[perf] gpu` "post" pass time rises with the ray count (0.06 -> 3.13 ms at
  256) — observed, not explained; likely clock/power state under sustained RT load.
- The frame TLAS is retired for two frames on capacity growth instead of being
  destroyed in flight (`pRtTlasRetired` in `gs_render.cpp`) — the AO dispatch was
  the first in-frame consumer of the TLAS and made the old destroy-while-recorded
  pattern visible in the validation layer.

**Path tracing is not planned.** A path tracer would be a second renderer that
has to re-author the game's look (its own BRDF, its own translation of the
`.2DM` materials, alpha tested foliage, the Flash / 2D UI) — that contradicts
the rule of this project, which is to reproduce retail from the data and the
readable code rather than to guess a nicer image. It would also throw away the
CPU-bound win that is the only reason RT is interesting here.

## 7. Verification recipe

```bat
:: capability, with the validation layer proving the chain is legal
build-glm\bin\opentdu2.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-glm-userdata ^
    -gm FREERIDE -skipcine -no3d -nopad -vk_enable_validation_layer -exitframe 40

:: the ray tracing self test: one known triangle, three rays, known answers
build-glm\bin\opentdu2.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-glm-userdata ^
    -gm FREERIDE -skipcine -no3d -nopad -rttest -vk_enable_validation_layer -exitframe 60

:: A/B: the same frame with the device gate forced off (the self test is skipped)
... -nort -rttest -exitframe 60
```

Expected log lines (`opentdu2.log`):

    [INFO] render_device.cpp: Using GPU: NVIDIA GeForce RTX 5070 Ti
    [INFO] render_device.cpp: Ray tracing: available (VK_KHR_ray_query)
    [INFO] render_device.cpp: Ray tracing: disabled (-nort)

With `-rttest` (the numbers are the expected ones, and they are exact):

    [INFO] render_device.cpp: Ray tracing limits: scratch alignment 128, max primitives 536870911
    [INFO] ray_tracing.cpp: Ray tracing self test: hit ray -> hit 1, t 5.000, instance 0, primitive 0
    [INFO] ray_tracing.cpp: Ray tracing self test: miss ray -> hit 0
    [INFO] ray_tracing.cpp: Ray tracing self test: rotated ray -> hit 1, t 5.000
    [INFO] ray_tracing.cpp: Ray tracing self test: PASSED

The three rays are the whole point: one through a triangle (must hit at t = 5),
one beside it (must miss), and one at the *rotated* triangle, which only hits if
the instance transform conversion is right.

With `-rtworld` (the world's own geometry, and what the numbers mean):

```bat
build-glm\bin\opentdu2.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-glm-userdata ^
    -gm FREERIDE -skipcine -nopad -rtworld -exitframe 200
```

    [INFO] eden_model.cpp: Ray tracing: 'library.3dd': 1539 of 1539 meshes have a BLAS (0 skipped)
    [INFO] gs_render.cpp: Ray tracing world: 1925 instances, TLAS 2631 KB, built in 0.53 ms, camera (36014.5, 7.0, 5816.5)
    [INFO] gs_render.cpp: Ray tracing world: down ray -> hit 1, t 1.62 m
    [INFO] gs_render.cpp: Ray tracing world: up ray -> hit 0, t 0.00 m
    [INFO] gs_render.cpp: Ray tracing world: forward ray -> hit 1, t 5.43 m, instance 0, primitive 34

The three probes are the check, and they are self consistent: the camera sits
1.62 m above something below it (the car), nothing is above it, and looking
forward it hits a mesh 5.43 m away. The camera is at a real world position
(`36014.5, 7.0, 5816.5` — Ibiza is 36 km across), and the numbers repeat frame
to frame.

**Costs, measured** (60 frames, FREERIDE, Ibiza): `-rtworld` adds **+2.2 s**, and
the same +2.2 s at 25 frames as at 60 — so it is the one-off BLAS build at load
(~2000 structures), not a per-frame cost. The per-frame TLAS build is 0.5-0.6 ms
for 1925-8930 instances. A default run (no `-rtworld`) allocates nothing for ray
tracing and is unchanged (frame diff 0.50, against a 0.54 same-build control).

**Caveat on frame diffs.** The map screen's Flash region
(x 78..542, y 60..141 at 1280x720) is *not deterministic between runs of the
same build* — measured 0.54 mean abs per channel, which is larger than the
0.36 measured across this change. A frame diff at that magnitude in that region
is not evidence of anything; compare a build against itself first.

## 8. Two conventions that are easy to get wrong

**The instance transform.** `VkTransformMatrixKHR` is a *row major* 3x4 matrix
whose last column is the translation (VUID-VkTransformMatrixKHR-matrix-03799: the
first three *columns* are the invertible 3x3). The Eden `Matrix4x4f` is *column
major* (`m[column][row]`, translation in `m[3]`, see `source/core/math.h`), so the
conversion is the transpose `vk.matrix[i][j] = eden.m[j][i]`
(`FillInstanceTransform` in `ray_tracing.cpp`). Getting it wrong silently turns
every rotation into its inverse — which is exactly what the rotated self test ray
catches. Do not "fix" that conversion without re-running `-rttest`.

**The SPIR-V version.** The device is created as Vulkan 1.1, so it accepts SPIR-V
1.4 only through `VK_KHR_spirv_1_4`. `glslangValidator -V --target-env vulkan1.2`
emits SPIR-V **1.5**, which that device rejects, while
`--target-env spirv1.4` emits 1.4 *and* keeps the Vulkan decorations
(`DescriptorSet`, `Binding`) because `-V` is used. That is what
`tools/compile_shaders.py` does for `.comp` (see `TARGET_ENV`), and
`--target-env vulkan1.1` cannot express a ray query at all.

## 9. What the self test proves, and what it does not

Proven by `-rttest` with the validation layer on and no VUID: the device
extensions and features, the VMA buffer device address flag, acceleration
structure storage and scratch allocation, a BLAS built from device addresses
(vertex format, stride, index type), a TLAS with an instance transform, the
SPIR-V 1.4 ray query shader, the acceleration structure descriptor, the push
constants, the host readback, and the hit distances themselves.

**Not** proven: real geometry. The game's meshes are sub ranges of shared vertex
and index buffers, they use two different position formats, and their instances
carry node and world matrices. That is step 3, and
`RayTracing::getBuildInputUsage()` exists so `EdenModel` can create its vertex
and index buffers with the flags a build input needs.

## 10. The multi-scene pitfall (this cost real time in step 3)

A frame does **not** draw one scene. `beginScene` is called by the environment
cube, the shadow casters (three slices), the ocean reflection, the planar
reflections and the post FX, each with its own camera and its own pass flags.
Two consequences, and both produced a silently empty instance list:

- **Clear the instance list once per frame, not in `beginScene`.** The list is
  `EdenRenderer::rtInstances`, cleared by `GSRender::beginFrame`
  (`clearRtInstances`). Clearing it in `beginScene` threw away everything
  captured so far in the frame, and the frame ends with a post FX pass.
- **`getSceneParams()` is the *last* scene**, i.e. a post FX pass with a zeroed
  camera — which is why the first probes reported `camera y 0.00` and missed
  everything. The camera of the scene that produced the instances is remembered
  with the first captured instance (`getRtCameraPosition`).

The capture gate is
`bRtCapture && !bShadowCaster && !bReflectionPass && !bDepthOnly && !scene.bMirroredView && mesh.BlasAddress != 0`.
The pass flags keep the shadow caster, reflection and depth prepass duplicates
out; the mirrored view flag keeps the environment cube faces out (same meshes
again, and they are what made the instance count swing between frames).

