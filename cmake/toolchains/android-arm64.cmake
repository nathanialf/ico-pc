# cmake/toolchains/android-arm64.cmake: Android arm64-v8a (preset
# android-arm64 and the Gradle build, android/app/build.gradle) with the
# NDK tools/fetch_android.sh installs: the NDK's own toolchain file, API
# level 29 (Android 10), the static C++ runtime (the program is C; the
# graphics driver loader, libadrenotools, is C++; v0.4.3 AN-22a), 16 KB-aligned
# segments, position-independent code (the program is libmain.so, which
# SDL's Java side loads).
#
# The NDK: ANDROID_NDK (the Gradle plugin passes it), else the environment's
# ANDROID_NDK_ROOT or ANDROID_NDK_HOME, else tools/toolchain/android.env.
if(NOT ANDROID_NDK)
    if(DEFINED ENV{ANDROID_NDK_ROOT} AND EXISTS "$ENV{ANDROID_NDK_ROOT}")
        set(ANDROID_NDK "$ENV{ANDROID_NDK_ROOT}")
    elseif(DEFINED ENV{ANDROID_NDK_HOME} AND EXISTS "$ENV{ANDROID_NDK_HOME}")
        set(ANDROID_NDK "$ENV{ANDROID_NDK_HOME}")
    else()
        set(_ico_android_env "${CMAKE_CURRENT_LIST_DIR}/../../tools/toolchain/android.env")
        if(EXISTS "${_ico_android_env}")
            file(STRINGS "${_ico_android_env}" _ico_ndk_line REGEX "^ANDROID_NDK_ROOT=")
            string(REGEX REPLACE "^ANDROID_NDK_ROOT=\"?([^\"]*)\"?$" "\\1" ANDROID_NDK
                                 "${_ico_ndk_line}")
        endif()
    endif()
endif()
if(NOT ANDROID_NDK OR NOT EXISTS "${ANDROID_NDK}/build/cmake/android.toolchain.cmake")
    message(FATAL_ERROR "android-arm64: no Android NDK found (run tools/fetch_android.sh, or "
                        "set ANDROID_NDK_ROOT)")
endif()
set(ANDROID_NDK "${ANDROID_NDK}" CACHE PATH "Android NDK")

set(ANDROID_ABI arm64-v8a)
if(NOT ANDROID_PLATFORM)
    set(ANDROID_PLATFORM android-29)
endif()
set(ANDROID_STL c++_static)
include("${ANDROID_NDK}/build/cmake/android.toolchain.cmake")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# NDK r28 already links arm64 with 16 KB pages; the flag keeps it so
string(APPEND CMAKE_SHARED_LINKER_FLAGS_INIT " -Wl,-z,max-page-size=16384")
string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " -Wl,-z,max-page-size=16384")
