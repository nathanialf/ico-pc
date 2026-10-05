# port/null: the devices the headless build runs without. Package 1B's
# gfx_null.c and debug_null.c are built from the top-level list. The system
# configuration (scf_null.c, until Phase 4F) is port/config's sysconf.c, the
# memory card port/save's mc_host.c (4D), the sound driver port/audio's
# ico_sndn2 (4B) and the pad port/input's (4C); this target gathers them for
# the tests that used the null devices.
add_library(ico_port_null INTERFACE)
target_include_directories(ico_port_null INTERFACE "${CMAKE_SOURCE_DIR}/port/input")
# the pad is port/input's libpad (pad_host.c; Phase 4C), which also holds the
# --pad-script reader; without live sources it is the scripted or empty pad.
# libscf is port/config's (sysconf.c; Phase 4F)
target_link_libraries(ico_port_null INTERFACE ico_port_data ico_input ico_config)

if(TARGET ico_pc)
    target_link_libraries(ico_pc PRIVATE ico_port_null)
endif()

# ico_input reads port/game/options.c, which reads port/config: host_config.c
add_executable(null_devices_test test/null_devices_test.c ../platform/host_config.c)
target_include_directories(null_devices_test PRIVATE "${ICO_COMPAT_DIR}")
target_compile_options(null_devices_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(null_devices_test PRIVATE ico_port_null)
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    add_test(NAME null_devices COMMAND null_devices_test)
endif()

# the pad script reader and the scripted pad as fumi/ios/pad.c sees it
add_executable(pad_script_test ../input/test/pad_script_test.c ../platform/host_config.c)
target_include_directories(pad_script_test PRIVATE "${ICO_COMPAT_DIR}")
target_compile_options(pad_script_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(pad_script_test PRIVATE ico_port_null)
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    add_test(NAME pad_script COMMAND pad_script_test)
endif()

# package 1D's double-click support for ico_pc (port/platform/host_config.c,
# which ico_pc compiles itself): ini, SHA-1, paths. Kept here with 1D's
# other tests.
add_executable(host_config_test ../platform/test/host_config_test.c ../platform/host_config.c)
target_include_directories(host_config_test PRIVATE "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_options(host_config_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(WIN32)
    target_link_libraries(host_config_test PRIVATE comdlg32 user32)
endif()
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    add_test(NAME host_config COMMAND host_config_test)
endif()
