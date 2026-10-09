# port/null: ico_port_null, an interface target that links the host's
# devices (port/data, port/input's pad, port/config's system configuration)
# into ico_pc and the tests below. gfx_null.c and libgcc_null.c (fptodp) are
# built from the top-level list.
add_library(ico_port_null INTERFACE)
target_include_directories(ico_port_null INTERFACE "${CMAKE_SOURCE_DIR}/port/input")
# the pad is port/input's libpad (pad_host.c), which also holds the
# --pad-script reader; without live sources it is the scripted or empty pad.
# libscf is port/config's (sysconf.c)
target_link_libraries(ico_port_null INTERFACE ico_port_data ico_input ico_config)

if(TARGET ico_pc)
    target_link_libraries(ico_pc PRIVATE ico_port_null)
endif()

# ico_input reads port/game/options.c, which reads port/config: host_config.c
add_executable(null_devices_test test/null_devices_test.c ../platform/host_config.c)
target_include_directories(null_devices_test PRIVATE "${ICO_COMPAT_DIR}")
target_compile_options(null_devices_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(null_devices_test PRIVATE ico_port_null)
if(ICO_RUN_TESTS)
    add_test(NAME null_devices COMMAND null_devices_test)
endif()

# the pad script reader and the scripted pad as fumi/ios/pad.c sees it
add_executable(pad_script_test ../input/test/pad_script_test.c ../platform/host_config.c)
target_include_directories(pad_script_test PRIVATE "${ICO_COMPAT_DIR}")
target_compile_options(pad_script_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(pad_script_test PRIVATE ico_port_null)
if(ICO_RUN_TESTS)
    add_test(NAME pad_script COMMAND pad_script_test)
endif()

# host_config.c, the double-click support of ico_pc (which compiles it
# itself): ini, SHA-1, paths. Built here beside the other tests of the
# host's devices.
add_executable(host_config_test ../platform/test/host_config_test.c ../platform/host_config.c)
target_include_directories(host_config_test PRIVATE "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_options(host_config_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME host_config COMMAND host_config_test)
endif()
# the same with ICO_HOST_FORCE_PORTABLE, as the Android build compiles
# host_config.c: the user folder is the executable's own. In a folder of its
# own, so the two never share an ico-pc.ini when ctest runs them together.
add_executable(host_config_portable_test ../platform/test/host_config_test.c
                                         ../platform/host_config.c)
target_include_directories(host_config_portable_test PRIVATE "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_options(host_config_portable_test PRIVATE ${ICO_SEMANTIC_OPTIONS}
                       ${ICO_PORT_WARNINGS})
target_compile_definitions(host_config_portable_test PRIVATE ICO_HOST_FORCE_PORTABLE=1)
set_target_properties(host_config_portable_test PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/portable")
if(ICO_RUN_TESTS)
    add_test(NAME host_config_portable COMMAND host_config_portable_test
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/portable")
endif()
