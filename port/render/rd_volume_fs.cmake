# rd_volume_fs.cmake: the shadow volumes drawn by a fragment entry with no output and
# no discard (rd_volume_fs.c, rd_volume_fs.h; docs/port/RENDER_POST.md, "The shadow
# volumes' stencil-only entry"), included from rd_perf_clock.cmake after ico_render and
# rd_replay_tool (port/render/CMakeLists.txt is near the 256 KiB cap,
# tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_volume_fs.c)

# rd_volume_fs_test: the pure parts (CPU, no device): the gate's reasons in their
# order, the volume key's date_clear_ps twin (the fragment entry alone differs),
# the volume key without a device (sprite_ps, counted as seen, none taken), the
# start-up set without a twin there, the switch's words, the bench's force, the
# attempt ledger's row
add_executable(rd_volume_fs_test test/rd_volume_fs_test.c)
target_compile_options(rd_volume_fs_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_volume_fs_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                     "${CMAKE_SOURCE_DIR}/port/rhi"
                                                     "${CMAKE_SOURCE_DIR}/port/shaders"
                                                     "${CMAKE_SOURCE_DIR}/port/platform")
target_link_libraries(rd_volume_fs_test PRIVATE ico_render)
if(COMMAND ico_copy_sdl3_dll)
    ico_copy_sdl3_dll(rd_volume_fs_test)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_volume_fs COMMAND rd_volume_fs_test)
endif()

# rd_volume_fs_selftest, rd_volume_fs_selftest_sabotage (test/rd_shadow_selftest.cmake
# through rd_fog_selftest_test, whose fog self-test's first run runs the volumes' own
# after the shadow count's): the self-test passed on the device (both frames' counts
# the same both ways, the Z GEQUAL frame apart from the Z ALWAYS one) and the entry on
# by the build's default; sabotaged (ICO_RD_VOLUME_FS=sabotage: each increment drawn
# as a decrement), the Z ALWAYS frame's count differs and the entry stays off.  The
# shadow count's own lines as rd_shadow_selftest expects them.  No device: SKIP.
if(ICO_RUN_TESTS AND TARGET rd_fog_selftest_test)
    set(_vf_st_env "")
    if(ICO_SANITIZE MATCHES "address")
        list(APPEND _vf_st_env "ASAN_OPTIONS=detect_leaks=0")
    endif()
    add_test(NAME rd_volume_fs_selftest
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                     "-DPATH=(one|seven) draws?"
                     "-DRESULTS=[^)]*"
                     "-DEXTRA=shadow volumes: the stencil-only entry \\(self-test: the count the same at [0-9]+ texels of [0-9]+x[0-9]+ at [0-9]+x[0-9]+ texels under Z ALWAYS and Z GEQUAL, step [48]; the Z GEQUAL frame differs from the Z ALWAYS one at [1-9][0-9]* texels\\): the same bytes"
                     "-DEXTRA2=no colour output, no discard\\): on \\(the build's default; self-test passed: "
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_shadow_selftest.cmake")
    add_test(NAME rd_volume_fs_selftest_sabotage
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                     "-DPATH=(one|seven) draws?"
                     "-DRESULTS=[^)]*"
                     "-DEXTRA=shadow volumes: self-test failed: the Z ALWAYS frame's count differs at [1-9][0-9]* of [0-9]+ texels"
                     "-DEXTRA2=no colour output, no discard\\): off until its self-test passes \\(ICO_RD_VOLUME_FS=sabotage; self-test failed at texel "
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_shadow_selftest.cmake")
    set_tests_properties(rd_volume_fs_selftest rd_volume_fs_selftest_sabotage
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
    if(_vf_st_env)
        set_tests_properties(rd_volume_fs_selftest PROPERTIES ENVIRONMENT "${_vf_st_env}")
    endif()
    set(_vf_st_sab ${_vf_st_env})
    list(APPEND _vf_st_sab "ICO_RD_VOLUME_FS=sabotage")
    set_tests_properties(rd_volume_fs_selftest_sabotage PROPERTIES ENVIRONMENT "${_vf_st_sab}")
endif()

# rd_volume_fs_ab (test/rd_volume_fs_ab.cmake): every dump of tools/shadow_cuts_dumps.txt,
# tools/date_cut_dumps.txt, tools/shadow_box_dumps.txt, tools/no_volume_dumps.txt and
# tools/lamps_dumps.txt (machine-local; a listed dump that is absent fails where
# /primary/dev/ico-dumps exists) with the volumes by date_clear_ps
# (ICO_RD_VOLUME_FS=1) against sprite_ps (ICO_RD_VOLUME_FS=0) at the PowerVR phone's
# setting, a desktop Enhanced 2x at 16:9, the Mali reporter's class at 1x and MSAA 4x
# at 2x: every PNG byte for byte, the same draws, passes and traffic, the self-test
# passed, every single-sampled volume draw by date_clear_ps; rd_volume_fs_sabotage the
# phone's setting with the self-test sabotaged (ICO_RD_VOLUME_FS=sabotage): refused,
# the same bytes.  No device: SKIP.  (No OpenGL twin: the entry is Vulkan's alone, and
# the tool runs its self-test only where ICO_RD_VOLUME_FS is set.)
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    set(_vf_env "")
    if(ICO_SANITIZE MATCHES "address")
        list(APPEND _vf_env "ASAN_OPTIONS=detect_leaks=0")
    endif()
    set(_vf_lists
        "${CMAKE_SOURCE_DIR}/tools/shadow_cuts_dumps.txt|${CMAKE_SOURCE_DIR}/tools/date_cut_dumps.txt|${CMAKE_SOURCE_DIR}/tools/shadow_box_dumps.txt|${CMAKE_SOURCE_DIR}/tools/no_volume_dumps.txt|${CMAKE_SOURCE_DIR}/tools/lamps_dumps.txt")
    foreach(_vf ab sabotage)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/volume_fs_${_vf}")
        set(_vf_sab "")
        if(_vf STREQUAL "sabotage")
            set(_vf_sab -DSABOTAGE=1)
        endif()
        add_test(NAME rd_volume_fs_${_vf}
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         "-DLISTS=${_vf_lists}" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/volume_fs_${_vf}"
                         ${_vf_sab} -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_volume_fs_ab.cmake")
        set_tests_properties(rd_volume_fs_${_vf} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 14400)
        if(_vf_env)
            set_tests_properties(rd_volume_fs_${_vf} PROPERTIES ENVIRONMENT "${_vf_env}")
        endif()
    endforeach()
endif()
