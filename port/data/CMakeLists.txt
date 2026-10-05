# port/data: the disc VFS (vfs.c) and its ISO9660 backend, the libcdvd host
# layer the game's fumi/ios/cdvd.c and seki/src/FileManager.c call, the SIF
# host layer (no IOP reboot, no IRX modules, RPC to host servers) and the
# IOP RAM stand-in. docs/port/DATA.md describes the design.
add_library(ico_port_data STATIC
    cdvd_host.c
    iop_ram.c
    iso9660.c
    sif_host.c
    vfs.c)
# port/platform is searched for package 1B's host_loop.h (the vsync
# callback list cdvd_host.c registers with when it exists).
target_include_directories(ico_port_data
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${ICO_COMPAT_DIR}" "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_options(ico_port_data PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
# cdvd_host.c registers with ico_platform's host loop. Tests that drive the
# vsync themselves define ico_host_on_vsync_register and pull nothing from it.
target_link_libraries(ico_port_data INTERFACE ico_platform)

if(TARGET ico_pc)
    target_link_libraries(ico_pc PRIVATE ico_port_data)
endif()

add_executable(vfs_test test/vfs_test.c)
target_include_directories(vfs_test PRIVATE "${ICO_COMPAT_DIR}")
target_compile_options(vfs_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(vfs_test PRIVATE ico_port_data)
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    # a synthetic image built by the test: runs everywhere, no disc needed
    add_test(NAME vfs_synthetic COMMAND vfs_test synth "${CMAKE_CURRENT_BINARY_DIR}")
    # the user's disc, read at run time; skipped (77) when it is absent
    set(ICO_DISC_IMAGE "${CMAKE_SOURCE_DIR}/baserom/Ico_PAL.iso" CACHE FILEPATH
        "The PAL disc image (SCES-50760) the disc tests read, never committed")
    add_test(NAME vfs_disc COMMAND vfs_test disc "${ICO_DISC_IMAGE}")
    set_tests_properties(vfs_disc PROPERTIES SKIP_RETURN_CODE 77)
endif()
