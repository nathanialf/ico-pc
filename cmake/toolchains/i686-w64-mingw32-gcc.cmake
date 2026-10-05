# cmake/toolchains/i686-w64-mingw32-gcc.cmake: Windows 32-bit with
# Debian's mingw-w64 gcc 14 (the win-x86-ref-gcc preset).
set(ICO_MINGW_TRIPLE i686-w64-mingw32)
set(CMAKE_SYSTEM_PROCESSOR x86)
include("${CMAKE_CURRENT_LIST_DIR}/mingw-gcc-common.cmake")
