# port/null: the devices the headless build runs without. This package's
# files are the pad, memory card, sound driver and system configuration;
# package 1B's gfx_null.c is built from its own list.
add_library(ico_port_null STATIC
    mc_null.c
    pad_null.c
    scf_null.c
    snd_null.c)
target_include_directories(ico_port_null
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${ICO_COMPAT_DIR}")
target_compile_options(ico_port_null PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(ico_port_null PUBLIC ico_port_data)

if(TARGET ico_pc)
    target_link_libraries(ico_pc PRIVATE ico_port_null)
endif()

add_executable(null_devices_test test/null_devices_test.c)
target_include_directories(null_devices_test PRIVATE "${ICO_COMPAT_DIR}")
target_compile_options(null_devices_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(null_devices_test PRIVATE ico_port_null)
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    add_test(NAME null_devices COMMAND null_devices_test)
endif()
