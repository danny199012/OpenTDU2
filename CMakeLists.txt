cmake_minimum_required( VERSION 3.24 )

project( OpenTDU2 VERSION 0.1.0 LANGUAGES C CXX )

set( CMAKE_CXX_STANDARD 17 )
set( CMAKE_CXX_STANDARD_REQUIRED True )
set( CMAKE_POSITION_INDEPENDENT_CODE ON )
set( CMAKE_EXPORT_COMPILE_COMMANDS ON )

if( NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES )
    set( CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE )
endif()

set( CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" )
set( CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib" )
set( CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib" )

set( OTDU_SRC_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/source" )
set( OTDU_THIRDPARTY_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/third_party" )

option( OTDU_DEVBUILD "Developer build (asserts on unimplemented functions)" OFF )

# Direct3D backends (docs/backends.md). The game renders with Vulkan today and
# -renderer <name> picks a backend at run time; D3D12 owns the device and the
# swapchain, D3D11 / D3D9 are scaffolding until their phase lands. Off Windows
# they are meaningless, so the options only do anything under WIN32.
option( OTDU_ENABLE_D3D12 "Build the Direct3D 12 backend (device + swapchain)" ON )
option( OTDU_ENABLE_D3D11 "Build the Direct3D 11 backend (not implemented yet)" OFF )
option( OTDU_ENABLE_D3D9 "Build the Direct3D 9 backend (not implemented yet)" OFF )

include( cmake/Dependencies.cmake )
add_subdirectory( "${OTDU_THIRDPARTY_FOLDER}/dxvk" )

# Engine (everything but the entry point) is built as a static library so the
# tools can reuse the asset readers.
file( GLOB_RECURSE OTDU_ENGINE_SRC CONFIGURE_DEPENDS
    "${OTDU_SRC_FOLDER}/*.cpp"
    "${OTDU_SRC_FOLDER}/*.h" )
list( FILTER OTDU_ENGINE_SRC EXCLUDE REGEX ".*/source/main\\.cpp$" )

add_library( otdu_engine STATIC ${OTDU_ENGINE_SRC} )
target_include_directories( otdu_engine PUBLIC "${OTDU_SRC_FOLDER}" )
# AMD FidelityFX API headers (FSR 3 plugin, source/render/upscalers)
target_include_directories( otdu_engine PRIVATE "${OTDU_THIRDPARTY_FOLDER}/fidelityfx-api" )
# NVIDIA DLSS (NGX) + Intel XeSS SDKs: local only, Windows plugins (the NGX
# import lib and the loader DLLs do not exist on other platforms; the guards
# keep a checked-out dlss-sdk/xess-sdk from breaking an Android configure).
if( WIN32 )
    if( EXISTS "${OTDU_THIRDPARTY_FOLDER}/dlss-sdk/include/nvsdk_ngx_vk.h" AND EXISTS "${OTDU_THIRDPARTY_FOLDER}/dlss-sdk/lib/nvsdk_ngx_d.lib" )
        target_include_directories( otdu_engine PRIVATE "${OTDU_THIRDPARTY_FOLDER}/dlss-sdk/include" )
        target_compile_definitions( otdu_engine PUBLIC OTDU_HAVE_DLSS=1 )
        target_link_libraries( otdu_engine PUBLIC "${OTDU_THIRDPARTY_FOLDER}/dlss-sdk/lib/nvsdk_ngx_d.lib" )
    endif()
    # Intel XeSS SDK headers: local only (not in the repository); without them the XeSS plugin compiles to a stub
    target_include_directories( otdu_engine PRIVATE "${OTDU_THIRDPARTY_FOLDER}/xess-sdk/inc" )
endif()
target_link_libraries( otdu_engine PUBLIC
    otdu_dxso otdu_volk otdu_vma otdu_stb otdu_stb_vorbis otdu_drlibs otdu_opusfile otdu_earcut otdu_imgui
    otdu_vulkan_headers SDL3::SDL3-static Jolt )
target_precompile_headers( otdu_engine PRIVATE "${OTDU_SRC_FOLDER}/pch.h" )
if( OTDU_DEVBUILD )
    target_compile_definitions( otdu_engine PUBLIC OTDU_DEVBUILD )
endif()
if( WIN32 )
    # Direct3D backends: the define is what source/render/rhi/render_backend.cpp
    # asks for ("is this backend compiled in?"), the libraries are the device,
    # the swapchain factory and (D3D9/11) the shader compiler.
    if( OTDU_ENABLE_D3D12 )
        target_compile_definitions( otdu_engine PUBLIC OTDU_D3D12=1 )
        # d3dcompiler: the 2D layer's HLSL is compiled at run time into SM 5.0
        # DXBC (D3D12 accepts it, so no dxc / SPIRV-Cross step is needed).
        target_link_libraries( otdu_engine PUBLIC d3d12 dxgi dxguid d3dcompiler )
    endif()
    if( OTDU_ENABLE_D3D11 )
        target_compile_definitions( otdu_engine PUBLIC OTDU_D3D11=1 )
        target_link_libraries( otdu_engine PUBLIC d3d11 dxgi dxguid d3dcompiler )
    endif()
    if( OTDU_ENABLE_D3D9 )
        target_compile_definitions( otdu_engine PUBLIC OTDU_D3D9=1 )
        target_link_libraries( otdu_engine PUBLIC d3d9 dxguid d3dcompiler )
    endif()
endif()
if( MSVC )
    target_compile_options( otdu_engine PRIVATE /W3 )
    target_compile_definitions( otdu_engine PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX )
else()
    target_compile_options( otdu_engine PRIVATE -Wall -Wno-unused-function -Wno-unused-variable -Wno-missing-braces )
endif()

if( ANDROID )
    # Android: the game is a shared library loaded by SDL's Java activity
    # (SDL_RunApp -> SDL_main, source/main.cpp includes SDL_main.h there). The
    # host tools and the Win32 launcher do not exist on this platform.
    add_library( opentdu2 SHARED "${OTDU_SRC_FOLDER}/main.cpp" )
else()
    add_executable( opentdu2 "${OTDU_SRC_FOLDER}/main.cpp" )
endif()
target_link_libraries( opentdu2 PRIVATE otdu_engine )
# Per-workspace executable name (WORKSPACES.md): build-cline / build-glm pass
# -DOTDU_EXE_SUFFIX=-cline / -glm, so each tree builds opentdu2-cline.exe /
# opentdu2-glm.exe and a "stop opentdu2" in one tree cannot kill another's run.
# Empty (the hub, launch\*.bat): opentdu2.exe. The target name stays opentdu2.
set( OTDU_EXE_SUFFIX "" CACHE STRING "Suffix of the game executable name (e.g. -cline)" )
if( OTDU_EXE_SUFFIX )
    set_target_properties( opentdu2 PROPERTIES OUTPUT_NAME "opentdu2${OTDU_EXE_SUFFIX}" )
endif()
if( APPLE )
    # Let volk find libvulkan/MoltenVK from the LunarG Vulkan SDK.
    if( DEFINED ENV{VULKAN_SDK} )
        set_target_properties( opentdu2 PROPERTIES BUILD_RPATH "$ENV{VULKAN_SDK}/lib;/usr/local/lib;/opt/homebrew/lib" )
    else()
        set_target_properties( opentdu2 PROPERTIES BUILD_RPATH "/usr/local/lib;/opt/homebrew/lib" )
    endif()
endif()

# Build-time build stamp (OTDU_BUILD_* in <build>/generated/build_info.h):
# the game banner, the tester watermark / window title and the launcher (its
# title, Home page and updater) read it.
include( cmake/BuildInfo.cmake )

if( NOT ANDROID )
    add_subdirectory( tools )
endif()
