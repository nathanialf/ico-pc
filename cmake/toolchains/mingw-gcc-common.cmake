# cmake/toolchains/mingw-gcc-common.cmake: Debian's mingw-w64 gcc, which
# tools/fetch_toolchain.sh unpacks under tools/toolchain/mingw-gcc;
# ICO_MINGW_GCC (environment) points elsewhere. ICO_MINGW_TRIPLE is set by
# the including file.
if(DEFINED ENV{ICO_MINGW_GCC})
    set(ICO_MINGW_GCC "$ENV{ICO_MINGW_GCC}")
else()
    get_filename_component(ICO_MINGW_GCC "${CMAKE_CURRENT_LIST_DIR}/../../tools/toolchain/mingw-gcc" ABSOLUTE)
endif()
set(_bin "${ICO_MINGW_GCC}/usr/bin/${ICO_MINGW_TRIPLE}")
if(NOT EXISTS "${_bin}-gcc-14-win32")
    message(FATAL_ERROR "mingw-w64 gcc not found at ${ICO_MINGW_GCC}: run tools/fetch_toolchain.sh "
                        "or set ICO_MINGW_GCC")
endif()
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ICO_MINGW_GCC)
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER "${_bin}-gcc-14-win32")
set(CMAKE_RC_COMPILER "${_bin}-windres")
set(CMAKE_AR "${_bin}-gcc-ar-win32" CACHE FILEPATH "")
set(CMAKE_RANLIB "${_bin}-gcc-ranlib-win32" CACHE FILEPATH "")
set(CMAKE_FIND_ROOT_PATH "${ICO_MINGW_GCC}/usr/${ICO_MINGW_TRIPLE}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
