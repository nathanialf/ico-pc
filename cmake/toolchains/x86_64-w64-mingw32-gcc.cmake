# cmake/toolchains/x86_64-w64-mingw32-gcc.cmake: Windows x64 with Debian's
# mingw-w64 gcc 14 (the win-x64-gcc preset).
set(ICO_MINGW_TRIPLE x86_64-w64-mingw32)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
include("${CMAKE_CURRENT_LIST_DIR}/mingw-gcc-common.cmake")
