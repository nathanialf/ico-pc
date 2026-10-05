# cmake/toolchains/x86_64-linux-clang.cmake: Linux x86-64 with llvm-mingw's
# clang (it targets Linux too) and lld, against the host's glibc and gcc
# runtime, which clang finds under /usr/lib/gcc.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
include("${CMAKE_CURRENT_LIST_DIR}/llvm-mingw-common.cmake")
set(CMAKE_C_COMPILER "${ICO_LLVM_MINGW}/bin/clang")
set(CMAKE_C_COMPILER_TARGET x86_64-linux-gnu)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")
