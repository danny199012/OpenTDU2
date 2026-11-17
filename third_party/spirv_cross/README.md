# SPIRV-Cross (vendored subset)

SPIR-V to HLSL translation for the Direct3D 12 backend
(`source/render/d3d12/d3d12_shader.*`, `docs/backends.md` section 4).

* Upstream: <https://github.com/KhronosGroup/SPIRV-Cross>
* Tag: `vulkan-sdk-1.4.350.0` (the same Vulkan SDK release the tree's
  Vulkan-Headers / SPIRV-Headers / volk are pinned to)
* License: Apache-2.0 (`LICENSE`)

Vendored instead of fetched at configure time because the D3D12 shader path is
on the critical path of every `.sob` shader and must build offline, exactly like
`third_party/dxvk` (the D3D9 bytecode half). Nothing in here is modified, so it
can be replaced wholesale from the tag above.

Only what the HLSL backend needs is present:

| Kept | |
|---|---|
| `spirv_cross.cpp`, `spirv_cross_parsed_ir.cpp`, `spirv_parser.cpp`, `spirv_cfg.cpp` | core |
| `spirv_glsl.cpp`, `spirv_hlsl.cpp` | the GLSL base emitter and the HLSL emitter |
| `spirv_cross_util.cpp` | helpers (`spirv_cross_util.hpp`) |
| `spirv.hpp`, `spirv_cross*.hpp`, `spirv_glsl.hpp`, `spirv_hlsl.hpp`, `spirv_cfg.hpp`, `spirv_cross_util.hpp`, `GLSL.std.450.h` | headers |
| `include/spirv_cross/*.hpp` | upstream's optional headers |

Not vendored: the CLI (`main.cpp`), the GLSL / MSL / C++ / reflection emitters,
the C API, and the test suite.
