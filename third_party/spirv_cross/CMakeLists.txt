# SPIRV-Cross (Apache-2.0, see LICENSE), the SPIR-V -> HLSL half of the
# Direct3D 12 shader path (docs/backends.md section 4). Vendored, not fetched:
# the project keeps its shader toolchain pinned to the Vulkan SDK tag the rest
# of the tree uses (vulkan-sdk-1.4.350.0) and builds offline.
#
# Only the sources the HLSL backend needs are here (core, parsed IR, parser,
# CFG, GLSL and HLSL emitters). GLSL / MSL / C++ / reflection emitters, the CLI
# and the test suite are not vendored.
file( GLOB OTDU_SPIRV_CROSS_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/spirv_cross.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/spirv_cross_parsed_ir.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/spirv_parser.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/spirv_cfg.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/spirv_glsl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/spirv_hlsl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/spirv_cross_util.cpp" )

add_library( otdu_spirv_cross STATIC ${OTDU_SPIRV_CROSS_SRC} )
target_include_directories( otdu_spirv_cross PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include" )
target_compile_features( otdu_spirv_cross PUBLIC cxx_std_17 )
# Third-party code: its warnings are not ours to keep clean (the dxvk subset is
# built the same way).
if( NOT MSVC )
    target_compile_options( otdu_spirv_cross PRIVATE -w )
endif()
