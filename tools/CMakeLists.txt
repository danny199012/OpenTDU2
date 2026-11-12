# Asset tools (offline helpers, the game itself reads the original files).
add_executable( otdu2_bnk_tool bnk_tool/main.cpp )
target_link_libraries( otdu2_bnk_tool PRIVATE otdu_engine )

add_executable( otdu2_sob_tool sob_tool/main.cpp )
target_link_libraries( otdu2_sob_tool PRIVATE otdu_engine )

add_executable( otdu2_cine_tool cine_tool/main.cpp cine_tool/manifest.cpp )
target_link_libraries( otdu2_cine_tool PRIVATE otdu_engine )

# The launcher (Windows only, tools/launcher): a Win32 + ImGui/D3D11 GUI,
# deliberately without the engine link - a tester distribution is this exe + the
# game exe + the user's own game data, and it builds in seconds. It shares the
# generated build stamp (cmake/BuildInfo.cmake) and embeds its own artwork and
# icon (launcher.rc.in, configured here with absolute paths) - the images
# tools/launcher/artwork/gen_art.py draws, no game assets.
if( WIN32 )
    set( OTDU_LAUNCHER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/launcher" )
    set( OTDU_LAUNCHER_ART_DIR "${OTDU_LAUNCHER_DIR}/artwork" )

    # The exe's version resource follows the release tag the build is for
    # (OTDU_RELEASE_TAG - the same env the build stamp reads; CI sets it on the
    # builds it publishes). A local build stays at 0.2.0.
    set( OTDU_VERSION_MAJOR 0 )
    set( OTDU_VERSION_MINOR 2 )
    set( OTDU_VERSION_PATCH 0 )
    if( DEFINED ENV{OTDU_RELEASE_TAG} AND NOT "$ENV{OTDU_RELEASE_TAG}" STREQUAL "" )
        string( REGEX MATCHALL "[0-9]+" _version_parts "$ENV{OTDU_RELEASE_TAG}" )
        list( LENGTH _version_parts _version_count )
        if( _version_count GREATER_EQUAL 3 )
            list( GET _version_parts 0 _v_major )
            list( GET _version_parts 1 _v_minor )
            list( GET _version_parts 2 _v_patch )
            set( OTDU_VERSION_MAJOR "${_v_major}" )
            set( OTDU_VERSION_MINOR "${_v_minor}" )
            set( OTDU_VERSION_PATCH "${_v_patch}" )
        endif()
    endif()
    set( OTDU_LAUNCHER_VERSION
         "${OTDU_VERSION_MAJOR}.${OTDU_VERSION_MINOR}.${OTDU_VERSION_PATCH}" )

    configure_file( "${OTDU_LAUNCHER_DIR}/launcher.rc.in"
                    "${CMAKE_CURRENT_BINARY_DIR}/launcher.rc" @ONLY )

    add_executable( otdu2_launcher WIN32
        launcher/main.cpp
        launcher/launcher_artwork.cpp
        launcher/launcher_http.cpp
        launcher/launcher_json.cpp
        launcher/launcher_settings.cpp
        launcher/launcher_ui.cpp
        launcher/launcher_updater.cpp
        launcher/launcher_util.cpp
        launcher/game_installer.cpp
        launcher/iso_archive.cpp
        # Dear ImGui (core + the Win32/DX11 backends; the engine's otdu_imgui
        # target only carries the SDL3 backend and links SDL, which a
        # stand-alone launcher does not need).
        "${imgui_SOURCE_DIR}/imgui.cpp"
        "${imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp"
        "${CMAKE_CURRENT_BINARY_DIR}/launcher.rc" )

    target_include_directories( otdu2_launcher PRIVATE
        "${OTDU_LAUNCHER_DIR}"
        "${CMAKE_BINARY_DIR}/generated"
        "${imgui_SOURCE_DIR}"
        "${imgui_SOURCE_DIR}/backends" )
    target_compile_definitions( otdu2_launcher PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS )
    target_link_libraries( otdu2_launcher PRIVATE
        user32 gdi32 shell32 ole32 oleaut32 uuid winhttp shlwapi
        d3d11 dxgi d3dcompiler dwmapi windowscodecs )
    add_dependencies( otdu2_launcher build_info )
    if( MSVC )
        target_compile_options( otdu2_launcher PRIVATE /W3 )
    else()
        target_compile_options( otdu2_launcher PRIVATE -Wno-unused-function
                                                        -Wno-unused-variable )
    endif()
endif()
