# rd_comp_spec.cmake: the shadow composites and levels drawn by their own entry,
# sprite_comp_ps (rd_comp_spec.c, rd_comp_spec.h; docs/port/RENDER_POST.md, "The shadow
# composites by their own entry"), included from rd_perf_clock.cmake after
# rd_volume_fs.cmake, after ico_render, rd_fog_selftest_test and rd_replay_tool
# (port/render/CMakeLists.txt is near the 256 KiB cap, tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_comp_spec.c)

# rd_comp_spec_test: the pure parts (CPU, no device): the key's forms (each field the
# forms hold refused alone), the DrawCB's words (every flag sprite_ps reads, alone and
# with the fixed ones, and the TEXA word), the twin (the fragment entry alone differs),
# a draw noted and turned without a device (counted as seen and kept, none taken, the
# pipeline left as it was), the start-up set without a twin there, the switch's words,
# the bench's force and its DATE set-up switch, the attempt ledger's row
add_executable(rd_comp_spec_test test/rd_comp_spec_test.c)
target_compile_options(rd_comp_spec_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_comp_spec_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                     "${CMAKE_SOURCE_DIR}/port/rhi"
                                                     "${CMAKE_SOURCE_DIR}/port/shaders"
                                                     "${CMAKE_SOURCE_DIR}/port/platform")
target_link_libraries(rd_comp_spec_test PRIVATE ico_render)
if(COMMAND ico_copy_sdl3_dll)
    ico_copy_sdl3_dll(rd_comp_spec_test)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_comp_spec COMMAND rd_comp_spec_test)
endif()

# rd_comp_spec_selftest, rd_comp_spec_selftest_sabotage (test/rd_shadow_selftest.cmake
# through rd_fog_selftest_test, whose fog self-test's first run runs this one after the
# volumes'): the self-test passed on the device (SCENE the same bytes both ways, the
# forms' draws taken) and the entry on by the build's default; sabotaged
# (ICO_RD_COMP_SPEC=sabotage: the second arm's band 2 with its vertex alpha one lower),
# SCENE differs and the entry stays off.  The shadow count's own lines matched loosely,
# as rd_volume_fs_selftest matches them.  No device: SKIP.
if(ICO_RUN_TESTS AND TARGET rd_fog_selftest_test)
    set(_cs_st_env "")
    if(ICO_SANITIZE MATCHES "address")
        list(APPEND _cs_st_env "ASAN_OPTIONS=detect_leaks=0")
    endif()
    add_test(NAME rd_comp_spec_selftest
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                     "-DPATH=(one|seven) draws?"
                     "-DRESULTS=[^)]*"
                     "-DEXTRA=composites: the shadow composites and levels by their own entry \\(self-test: SCENE the same at [0-9]+ texels, [0-9]+x[0-9]+ at [0-9]+x[0-9]+ texels, [4-9] composite and level draws each way\\): the same bytes"
                     "-DEXTRA2=sprite_ps's path for them alone\\): on \\(the build's default; self-test passed: "
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_shadow_selftest.cmake")
    add_test(NAME rd_comp_spec_selftest_sabotage
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                     "-DPATH=(one|seven) draws?"
                     "-DRESULTS=[^)]*"
                     "-DEXTRA=composites: self-test failed: SCENE differs at [1-9][0-9]* of [0-9]+ texels"
                     "-DEXTRA2=sprite_ps's path for them alone\\): off until its self-test passes \\(ICO_RD_COMP_SPEC=sabotage; self-test failed at texel "
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_shadow_selftest.cmake")
    set_tests_properties(rd_comp_spec_selftest rd_comp_spec_selftest_sabotage
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
    if(_cs_st_env)
        set_tests_properties(rd_comp_spec_selftest PROPERTIES ENVIRONMENT "${_cs_st_env}")
    endif()
    set(_cs_st_sab ${_cs_st_env})
    list(APPEND _cs_st_sab "ICO_RD_COMP_SPEC=sabotage")
    set_tests_properties(rd_comp_spec_selftest_sabotage PROPERTIES ENVIRONMENT "${_cs_st_sab}")
endif()

# rd_comp_spec_ab (test/rd_comp_spec_ab.cmake): every dump of tools/shadow_cuts_dumps.txt,
# tools/date_cut_dumps.txt, tools/shadow_box_dumps.txt, tools/no_volume_dumps.txt,
# tools/lamps_dumps.txt and tools/glow_light_dumps.txt (machine-local; a listed dump that
# is absent fails where /primary/dev/ico-dumps exists) with the composites and levels by
# sprite_comp_ps (ICO_RD_COMP_SPEC=1) against sprite_ps (ICO_RD_COMP_SPEC=0) at the
# PowerVR phone's setting and a desktop Enhanced 2x at 16:9, and the first list at the
# Mali reporter's class (not offered: no dual-source blend) and MSAA 4x at 2x: every PNG
# byte for byte, the same draws, passes and traffic, the self-test passed, at least one
# draw by it; rd_comp_spec_sabotage the phone's setting with the self-test sabotaged
# (ICO_RD_COMP_SPEC=sabotage): refused, the same bytes.  No device: SKIP.  (No OpenGL
# twin: the entry is Vulkan's alone, and the tool runs its self-test only where
# ICO_RD_COMP_SPEC is set.)
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    set(_cs_env "")
    if(ICO_SANITIZE MATCHES "address")
        list(APPEND _cs_env "ASAN_OPTIONS=detect_leaks=0")
    endif()
    set(_cs_lists
        "${CMAKE_SOURCE_DIR}/tools/shadow_cuts_dumps.txt|${CMAKE_SOURCE_DIR}/tools/date_cut_dumps.txt|${CMAKE_SOURCE_DIR}/tools/shadow_box_dumps.txt|${CMAKE_SOURCE_DIR}/tools/no_volume_dumps.txt|${CMAKE_SOURCE_DIR}/tools/lamps_dumps.txt|${CMAKE_SOURCE_DIR}/tools/glow_light_dumps.txt")
    foreach(_cs ab sabotage)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/comp_spec_${_cs}")
        set(_cs_sab "")
        if(_cs STREQUAL "sabotage")
            set(_cs_sab -DSABOTAGE=1)
        endif()
        add_test(NAME rd_comp_spec_${_cs}
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         "-DLISTS=${_cs_lists}" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/comp_spec_${_cs}"
                         ${_cs_sab} -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_comp_spec_ab.cmake")
        set_tests_properties(rd_comp_spec_${_cs} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 14400)
        if(_cs_env)
            set_tests_properties(rd_comp_spec_${_cs} PROPERTIES ENVIRONMENT "${_cs_env}")
        endif()
    endforeach()
endif()
