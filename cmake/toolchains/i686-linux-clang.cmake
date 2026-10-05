# cmake/toolchains/i686-linux-clang.cmake: Linux i386 (the ref-m32 preset),
# llvm-mingw's clang and lld against tools/toolchain/sysroot-i386, which
# tools/fetch_toolchain.sh builds from Debian's biarch glibc and libgcc
# packages over the host's headers.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR i686)
include("${CMAKE_CURRENT_LIST_DIR}/llvm-mingw-common.cmake")
if(DEFINED ENV{ICO_SYSROOT_I386})
    set(ICO_SYSROOT_I386 "$ENV{ICO_SYSROOT_I386}")
else()
    get_filename_component(ICO_SYSROOT_I386 "${CMAKE_CURRENT_LIST_DIR}/../../tools/toolchain/sysroot-i386" ABSOLUTE)
endif()
if(NOT EXISTS "${ICO_SYSROOT_I386}/usr/include/x86_64-linux-gnu/gnu/stubs-32.h")
    message(FATAL_ERROR "i386 sysroot not found at ${ICO_SYSROOT_I386}: run tools/fetch_toolchain.sh "
                        "or set ICO_SYSROOT_I386")
endif()
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ICO_SYSROOT_I386)
set(CMAKE_SYSROOT "${ICO_SYSROOT_I386}")
set(CMAKE_C_COMPILER "${ICO_LLVM_MINGW}/bin/clang")
set(CMAKE_C_COMPILER_TARGET i686-linux-gnu)
# Debian keeps the biarch glibc headers under the 64-bit multiarch directory.
set(CMAKE_C_FLAGS_INIT "-idirafter ${ICO_SYSROOT_I386}/usr/include/x86_64-linux-gnu")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")
