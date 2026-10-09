# port/math: the VU0 maths in C and the newlib copies.
# Compiled with the game's semantics options (-ffp-contract=off and the rest,
# cmake/IcoFlags.cmake) and the port warning set; port/compat is not on the
# include path, so the host libm here is the host's own.
add_library(ico_math STATIC
    cloth.c
    libvu0.c
    matrix.c
    matrix_drive.c
    matrix_stack.c
    ps2float.c
    quaternion.c
    softdouble.c
    newlib/ico_libm.c
    newlib/qsort.c
    newlib/rand.c)
target_compile_options(ico_math PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(NOT WIN32)
    target_link_libraries(ico_math PUBLIC m)
endif()
if(TARGET ico_pc)
    target_link_libraries(ico_pc PRIVATE ico_math)
endif()

add_executable(math_test test/math_test.c)
target_compile_options(math_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(math_test PRIVATE ico_math ico_platform)

add_executable(newlib_test test/newlib_test.c)
target_compile_options(newlib_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(newlib_test PRIVATE ico_math ico_platform)

# softdouble_test: the oracle is the EE's soft float itself, sce/libgcc's
# dp-bit.c and fp-bit.c, copied into the build by dpbit_harness.cmake (host
# return types for the functions that returned through a register) and
# compiled with every exported name prefixed ref_. Their bitfield unions need
# the GCC layout, so not on Windows (mingw's ms_struct default).
if(NOT WIN32)
    set(ICO_DPBIT_DIR "${CMAKE_CURRENT_BINARY_DIR}/dpbit")
    set(ICO_DPBIT_SRC "${CMAKE_SOURCE_DIR}/sce/libgcc")
    add_custom_command(
        OUTPUT "${ICO_DPBIT_DIR}/dp-bit.c" "${ICO_DPBIT_DIR}/fp-bit.c" "${ICO_DPBIT_DIR}/libgcc2.h"
        COMMAND ${CMAKE_COMMAND} -DSRC=${ICO_DPBIT_SRC} -DDST=${ICO_DPBIT_DIR}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/test/dpbit_harness.cmake"
        DEPENDS "${ICO_DPBIT_SRC}/dp-bit.c" "${ICO_DPBIT_SRC}/fp-bit.c"
                "${ICO_DPBIT_SRC}/libgcc2.h" "${CMAKE_CURRENT_SOURCE_DIR}/test/dpbit_harness.cmake"
        COMMENT "softdouble oracle: host copy of sce/libgcc dp-bit.c, fp-bit.c"
        VERBATIM)
    set(ICO_DPBIT_RENAMES)
    foreach(sym dpadd dpsub dpmul dpdiv dpcmp litodp dptoli dptoul __negdf2 __make_dp dptofp
                __pack_d __unpack_d _fpadd_parts __fpcmp_parts_d fpadd fpsub fpmul fpdiv fpcmp
                sitofp fptosi fptoui __negsf2 __make_fp fptodp __pack_f __unpack_f
                __fpcmp_parts_f)
        list(APPEND ICO_DPBIT_RENAMES "${sym}=ref_${sym}")
    endforeach()
    add_library(ico_dpbit_ref STATIC "${ICO_DPBIT_DIR}/dp-bit.c" "${ICO_DPBIT_DIR}/fp-bit.c")
    target_compile_definitions(ico_dpbit_ref PRIVATE ${ICO_DPBIT_RENAMES})
    target_compile_options(ico_dpbit_ref PRIVATE -w -fno-builtin -fno-strict-aliasing)
    # The period code reads locals it never set: dptofp passes the
    # normal_exp __unpack_d leaves unset for a NaN or infinity, which
    # __pack_f then ignores. clang's -O2 treats that uninitialized argument
    # as undefined behaviour and folds dptofp of a NaN to infinity; zeroing
    # the locals gives the read a value, as the EE's stack always did.
    include(CheckCCompilerFlag)
    check_c_compiler_flag(-ftrivial-auto-var-init=zero ICO_HAS_AUTO_VAR_INIT_ZERO)
    if(ICO_HAS_AUTO_VAR_INIT_ZERO)
        target_compile_options(ico_dpbit_ref PRIVATE -ftrivial-auto-var-init=zero)
    endif()
    # and shifts a fraction into the sign bit of a long long (fptodp), as
    # the EE's sll did; UBSan (the asan preset) would stop there
    if(ICO_SANITIZE)
        target_compile_options(ico_dpbit_ref PRIVATE -fno-sanitize=shift)
    endif()

    add_executable(softdouble_test test/softdouble_test.c)
    target_compile_options(softdouble_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    target_link_libraries(softdouble_test PRIVATE ico_math ico_dpbit_ref)
endif()

if(ICO_RUN_TESTS)
    add_test(NAME math COMMAND math_test)
    add_test(NAME newlib COMMAND newlib_test)
    if(TARGET softdouble_test)
        add_test(NAME softdouble COMMAND softdouble_test)
    endif()
endif()

# softdouble_gate: the game functions that did
# double arithmetic through the EE's soft float contain no `double` and no
# double constant outside ICO_D(), and compile without a float-to-double
# promotion (-Wdouble-promotion, through this build's compile_commands.json).
if(Python3_FOUND AND CMAKE_EXPORT_COMPILE_COMMANDS AND NOT CMAKE_CROSSCOMPILING)
    add_test(NAME softdouble_gate
             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/softdouble_gate.py"
                     --build "${CMAKE_BINARY_DIR}")
endif()
