# port/math: the VU0 maths in C (docs/port/MATH.md) and the newlib copies.
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

if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    add_test(NAME math COMMAND math_test)
    add_test(NAME newlib COMMAND newlib_test)
endif()
