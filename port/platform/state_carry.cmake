# state_carry.cmake: the save states' carry-over guard (issue 217; docs/port/
# PLATFORM.md "The carry-over guard", docs/port/TESTING.md), included at the
# end of port/platform/CMakeLists.txt.
#
# state_carry_probe: the save side's verdict on a program image in a file,
# from the product's own state_xbuild.c and state_syms.c (tools/
# state_carry_check.py drives it; the packaging scripts and CI build the same
# sources with the host compiler when there is no CMake host build).  Built
# here with ICO_CARRY_PROBE_SELF, which adds --self-image (state_check.c's
# ico_state_image_range of the probe itself).
if(ANDROID OR IOS)
    return()
endif()
add_executable(state_carry_probe state_carry_probe.c)
target_compile_definitions(state_carry_probe PRIVATE ICO_CARRY_PROBE_SELF=1)
target_compile_options(state_carry_probe PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(state_carry_probe PRIVATE ico_platform)

if(NOT ICO_RUN_TESTS OR NOT Python3_EXECUTABLE OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()
set(_carry_py "${Python3_EXECUTABLE}" -I "${CMAKE_SOURCE_DIR}/tools/state_carry_check.py")

# state_carry_check: the tool's self-test.  port/platform/test/
# state_carry_fixture.c compiled and linked at test time in every layout the
# host has compilers for: gcc (GNU ld) with read-only data in its own
# segment (-z separate-code) and inside the executable one (-z
# noseparate-code); where the toolchain's llvm-mingw is (x86-64 hosts:
# tools/fetch_toolchain.sh), its clang and lld for arm64 Android and x86-64
# Linux, each with --rosegment and --no-rosegment (issue 217's layout); where
# the toolchain's mingw-w64 gcc is, a Windows program at 0x140000000.  Each
# layout is checked as made (its read-only segments), judged by the probe
# with an exact symbol file (status 0, 6 targets, none unnamed) and with one
# that leaves the table's function pointer in code no unit holds (status 1,
# exactly one unnamed).  The fixture's own C compiler is gcc: the presets'
# compiler may be clang (linux-x64-clang), which keeps the file's order
# without -fno-toplevel-reorder; the tool asks the compiler which it is.
find_program(ICO_CARRY_GCC NAMES gcc)
set(_carry_args selftest --workdir "${CMAKE_CURRENT_BINARY_DIR}/state_carry_check"
                --probe $<TARGET_FILE:state_carry_probe>)
if(ICO_CARRY_GCC)
    list(APPEND _carry_args --cc "${ICO_CARRY_GCC}")
else()
    list(APPEND _carry_args --cc "${CMAKE_C_COMPILER}")
endif()
set(_carry_llvm "${CMAKE_SOURCE_DIR}/tools/toolchain/llvm-mingw/bin/clang")
if(EXISTS "${_carry_llvm}")
    list(APPEND _carry_args --lld-clang "${_carry_llvm}")
endif()
if(DEFINED ENV{ICO_MINGW_GCC})
    set(_carry_mingw "$ENV{ICO_MINGW_GCC}/usr/bin/x86_64-w64-mingw32-gcc-14-win32")
else()
    set(_carry_mingw
        "${CMAKE_SOURCE_DIR}/tools/toolchain/mingw-gcc/usr/bin/x86_64-w64-mingw32-gcc-14-win32")
endif()
if(EXISTS "${_carry_mingw}")
    list(APPEND _carry_args --mingw-gcc "${_carry_mingw}")
endif()
add_test(NAME state_carry_check COMMAND ${_carry_py} ${_carry_args})
# passes with every layout the host's compilers make judged (1 on a host
# whose linker cannot make elf-separate and that has no clang or mingw, as
# linux-arm64's GNU ld may be: that one is named "not judged"); judging none
# fails (FAIL)
set_tests_properties(state_carry_check PROPERTIES PASS_REGULAR_EXPRESSION
                     "selftest: [1-7] layouts checked" FAIL_REGULAR_EXPRESSION "FAIL")

# state_carry_image: the one rule the tool mirrors instead of running (the
# loader's view: ico_state_image_range's image, code and read-only ranges)
# read from the probe's own file and compared with the function's answer in
# the probe's process
# state_carry_probe_build: the probe built as the package scripts build it
# (the host compiler over the tool's own source list, not this build's
# target): v0.11.9's rc3 and rc4 packages stopped at "cannot judge" because
# the save rules had come to need a source the list lacked
add_test(NAME state_carry_probe_build
         COMMAND ${_carry_py} probe-build --workdir "${CMAKE_CURRENT_BINARY_DIR}/state_carry_probe_build"
                 --cc ${CMAKE_C_COMPILER})
set_tests_properties(state_carry_probe_build PROPERTIES PASS_REGULAR_EXPRESSION
                     "probe-build: the standalone probe built")

add_test(NAME state_carry_image COMMAND ${_carry_py} self-image --probe $<TARGET_FILE:state_carry_probe>)
set_tests_properties(state_carry_image PROPERTIES PASS_REGULAR_EXPRESSION
                     "self-image: the file's image, code and read-only ranges equal")

# state_carry_xbuild: the real mechanism across two real links of one
# program (port/platform/test/state_carry_xbuild_test.c): A as the desktop
# programs are linked, B with more code and texts ahead of A's and its
# read-only data inside the executable segment (-z noseparate-code, with
# --no-rosegment where the linker has it; A -z separate-code), each reading its own link
# map; A's state loads into B and B's into A through ico_xb_finish,
# ico_xb_check and ico_state_reloc_word, every word becoming the loading
# build's own address of the same thing
include(CheckLinkerFlag)
check_linker_flag(C "LINKER:--no-rosegment" ICO_LD_NO_ROSEGMENT)
check_linker_flag(C "LINKER:-z,noseparate-code" ICO_LD_NOSEPARATE_CODE)
if(NOT ICO_LD_NOSEPARATE_CODE)
    message(STATUS "state_carry_xbuild: the linker cannot put read-only data inside the "
                   "executable segment: the test is not built")
    return()
endif()
# not under AddressSanitizer: the save reads the program's read-only data
# whole (state_xbuild.c ro_freq, find_in: a text's window), which in a
# sanitizer build holds ASan's poisoned red zones after each literal of
# ico_platform, so the first save would stop on a global-buffer-overflow.
# The program itself never saves a state in a sanitizer build (state_host.c
# STATE_SANITIZED turns save states off), so no product path reads them there.
if(ICO_SANITIZE MATCHES "address")
    message(STATUS "state_carry_xbuild: not built under AddressSanitizer (the save reads the "
                   "read-only data whole; save states are off in a sanitizer build)")
    return()
endif()
foreach(_v a b)
    add_executable(state_carry_xbuild_${_v} test/state_carry_xbuild_test.c)
    target_include_directories(state_carry_xbuild_${_v} PRIVATE "${ICO_COMPAT_DIR}")
    target_compile_options(state_carry_xbuild_${_v} PRIVATE ${ICO_SEMANTIC_OPTIONS}
                                                             ${ICO_PORT_WARNINGS})
    target_link_libraries(state_carry_xbuild_${_v} PRIVATE ico_platform)
    target_link_options(state_carry_xbuild_${_v} PRIVATE
        "LINKER:-Map=$<TARGET_FILE_DIR:state_carry_xbuild_${_v}>/state_carry_xbuild_${_v}.map")
endforeach()
target_compile_definitions(state_carry_xbuild_b PRIVATE CARRY_B=1)
# B: -z noseparate-code puts GNU ld's read-only data in the executable
# segment; lld also needs --no-rosegment, which GNU ld 2.43+ accepts too
# (keeping the merged layout: checked by hand with ld 2.44). GNU ld's own
# --rosegment/--no-rosegment (2.43+) do NOT merge on their own: the first
# version of this test linked B with --no-rosegment alone and GNU ld 2.44
# gave R, R+E, R, RW. A: -z separate-code, its own segment for read-only
# data with either linker. The test's layout lines check what came out.
if(ICO_LD_NOSEPARATE_CODE)
    target_link_options(state_carry_xbuild_a PRIVATE "LINKER:-z,separate-code")
    target_link_options(state_carry_xbuild_b PRIVATE "LINKER:-z,noseparate-code")
endif()
if(ICO_LD_NO_ROSEGMENT)
    target_link_options(state_carry_xbuild_b PRIVATE "LINKER:--no-rosegment")
endif()
add_test(NAME state_carry_xbuild
         COMMAND ${_carry_py} xbuild --a $<TARGET_FILE:state_carry_xbuild_a>
                 --b $<TARGET_FILE:state_carry_xbuild_b>
                 --a-map $<TARGET_FILE_DIR:state_carry_xbuild_a>/state_carry_xbuild_a.map
                 --b-map $<TARGET_FILE_DIR:state_carry_xbuild_b>/state_carry_xbuild_b.map
                 --workdir "${CMAKE_CURRENT_BINARY_DIR}/state_carry_xbuild")
set_tests_properties(state_carry_xbuild PROPERTIES PASS_REGULAR_EXPRESSION
                     "xbuild: every word of each build's state translated")
