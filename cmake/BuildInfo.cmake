# Build-time build stamp (OTDU_BUILD_* in <build>/generated/build_info.h): the
# build number (git rev-list --count HEAD), the short hash and the dirty flag.
#
# It has to run at BUILD time, not configure time: build.ps1 only reconfigures
# when the CMake cache is missing, so a configure-time stamp would go stale
# between reconfigures. The build_info target re-probes git on every ninja
# invocation and rewrites the header copy-if-different (StampBuildInfo.cmake),
# so a no-op rebuild recompiles nothing. Include it as:
#
#   #include "build_info.h"   // OTDU_BUILD_STRING "build 1224 (60bc869)" etc.
#
# Consumers: the game banner, the tester window title, the tester watermark
# (source/debug/watermark_overlay), the launcher's title / Home page and the
# launcher's updater (which compares the running build against origin/main).
# No git / not a repository (a tester's source zip): everything still builds,
# stamped "unknown".

find_package( Git QUIET )

set( OTDU_BUILD_INFO_DIR "${CMAKE_BINARY_DIR}/generated" )
set( OTDU_BUILD_INFO_FILE "${OTDU_BUILD_INFO_DIR}/build_info.h" )

add_custom_target( build_info
    BYPRODUCTS "${OTDU_BUILD_INFO_FILE}"
    COMMAND ${CMAKE_COMMAND}
        -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DOUT_FILE=${OTDU_BUILD_INFO_FILE}
        -P ${CMAKE_CURRENT_LIST_DIR}/StampBuildInfo.cmake
    COMMENT "Stamping build info (git build number / hash / dirty)"
    VERBATIM )

# The engine's consumers (the game exe) get the include dir through the PUBLIC
# propagation; the launcher does not link the engine and adds it itself
# (tools/CMakeLists.txt).
add_dependencies( otdu_engine build_info )
target_include_directories( otdu_engine PUBLIC "${OTDU_BUILD_INFO_DIR}" )
