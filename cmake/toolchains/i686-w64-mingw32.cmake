# cmake/toolchains/i686-w64-mingw32.cmake: Windows 32-bit, cross-compiled with
# llvm-mingw's clang and lld against its mingw-w64 UCRT runtime.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86)
include("${CMAKE_CURRENT_LIST_DIR}/llvm-mingw-common.cmake")
set(CMAKE_C_COMPILER "${ICO_LLVM_MINGW}/bin/i686-w64-mingw32-clang")
set(CMAKE_CXX_COMPILER "${ICO_LLVM_MINGW}/bin/i686-w64-mingw32-clang++")
set(CMAKE_RC_COMPILER "${ICO_LLVM_MINGW}/bin/i686-w64-mingw32-windres")
set(CMAKE_FIND_ROOT_PATH "${ICO_LLVM_MINGW}/i686-w64-mingw32")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
