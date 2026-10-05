# cmake/toolchains/llvm-mingw-common.cmake: the llvm-mingw install the
# toolchain files share. tools/fetch_toolchain.sh puts it under
# tools/toolchain/llvm-mingw; ICO_LLVM_MINGW (environment) points elsewhere.
if(DEFINED ENV{ICO_LLVM_MINGW})
    set(ICO_LLVM_MINGW "$ENV{ICO_LLVM_MINGW}")
else()
    get_filename_component(ICO_LLVM_MINGW "${CMAKE_CURRENT_LIST_DIR}/../../tools/toolchain/llvm-mingw" ABSOLUTE)
endif()
if(NOT EXISTS "${ICO_LLVM_MINGW}/bin/clang")
    message(FATAL_ERROR "llvm-mingw not found at ${ICO_LLVM_MINGW}: run tools/fetch_toolchain.sh "
                        "or set ICO_LLVM_MINGW")
endif()
# try_compile projects re-read this file; pass the variable through.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ICO_LLVM_MINGW)
set(CMAKE_AR "${ICO_LLVM_MINGW}/bin/llvm-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${ICO_LLVM_MINGW}/bin/llvm-ranlib" CACHE FILEPATH "")
