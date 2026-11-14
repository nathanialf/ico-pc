# rd_health.cmake: the picture-health guard (rd_health.c the rule and the
# mark, rd_health_live.c the renderer's part; rd_health.h; issue 179),
# included at the end of port/render/CMakeLists.txt (a file of its own:
# port/render/CMakeLists.txt is near the 256 KiB cap, tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_health.c rd_health_live.c)

# rd_health_test: the pure parts (CPU): the rule over the four logs'
# patterns (the Mali-G52's v0.11.6 runs step within 16 pictures; the same
# phone's v0.11.5 run, the Adreno 740's isolated hangs and its restarts, an
# idle GPU's restarts never do), each step's groups and parts, a recovery
# naming the culprit, no recovery ending without a loop, the next-start
# parts confirmed at the next start, and the mark: formatted, parsed back,
# kept for this version and dropped after an update or in another form
add_executable(rd_health_test test/rd_health_test.c)
target_compile_options(rd_health_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_health_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                  "${CMAKE_SOURCE_DIR}/port/rhi"
                                                  "${CMAKE_SOURCE_DIR}/port/shaders"
                                                  "${CMAKE_SOURCE_DIR}/port/platform")
target_link_libraries(rd_health_test PRIVATE ico_render)
if(ICO_RUN_TESTS)
    add_test(NAME rd_health COMMAND rd_health_test)
endif()

# rd_vuprepass_selftest_compact_stamps: the index compaction's start-up
# self-test fails when its own pictures carry the GPU's own sign of a reset
# (ICO_RD_HEALTH_FAKE=selftest reads every record collected inside it as a
# hang, under ICO_RD_VU_COMPACT=1 and the record by compute dispatches): the
# record's test with it fails with those words, again without it passes, the
# compaction refused for the run, the pre-pass on
if(ICO_RUN_TESTS AND TARGET rd_vuprepass_selftest_test AND DEFINED _vst)
    add_test(NAME rd_vuprepass_selftest_compact_stamps
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: picture health: ICO_RD_HEALTH_FAKE=selftest: .*rd: VU pre-pass: the record with the index compaction failed the self-test \\(the GPU's own sign of a reset in [1-9][0-9]* of the test's [1-9][0-9]* pictures .*rd: VU index compaction: refused by its start-up self-test \\(the record's test with it: the GPU's own sign of a reset in "
                     "-DNOT_=${_cmp_copy}" -P "${_vst}")
    set_tests_properties(rd_vuprepass_selftest_compact_stamps
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                         ENVIRONMENT "${_env};ICO_RD_VU_RECORD=compute;ICO_RD_VU_COMPACT=1;ICO_RD_HEALTH_FAKE=selftest")
endif()

# rd_health_ab (test/rd_health_ab.cmake): issue 179's Mali-G52 stage 3 frame
# under the Arm fakes, every group the guard switches in the run on, the
# guard driven by ICO_RD_HEALTH_FAKE: every step's line, the recovery's line
# naming each group, and the picture after the steps equal to the picture
# without them byte for byte.  No device, or no dump folder: SKIP.
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/health_ab")
    add_test(NAME rd_health_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=/primary/dev/ico-dumps/v0116-mali179/frame-20261112-195947-v17565.zip"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/health_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_health_ab.cmake")
    set_tests_properties(rd_health_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 3600)
    if(ICO_SANITIZE MATCHES "address")
        # the lavapipe driver's exit leak (port/render/CMakeLists.txt, the
        # replay tool's tests under asan)
        set_property(TEST rd_health_ab APPEND PROPERTY ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
    endif()
endif()
