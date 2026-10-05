# cmake/toolchains/i686-linux-gcc.cmake: Linux i386 with the host gcc in
# -m32 mode (the ref-m32-gcc preset), against the same
# tools/toolchain/sysroot-i386 as i686-linux-clang.cmake. The host gcc has
# no 32-bit libgcc of its own; the sysroot's lib32gcc-14-dev provides it.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR i686)
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
set(CMAKE_C_COMPILER gcc)
set(_libgcc32 "${ICO_SYSROOT_I386}/usr/lib/gcc/x86_64-linux-gnu/14/32")
set(CMAKE_C_FLAGS_INIT "-m32 -idirafter ${ICO_SYSROOT_I386}/usr/include/x86_64-linux-gnu")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-m32 -B${_libgcc32} -B${ICO_SYSROOT_I386}/usr/lib32 -L${_libgcc32}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${CMAKE_EXE_LINKER_FLAGS_INIT}")
