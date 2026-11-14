# rd_health.cmake: the picture-health report (rd_health.c the rule,
# rd_health_live.c the renderer's part; rd_health.h; issue 179),
# included at the end of port/render/CMakeLists.txt (a file of its own:
# port/render/CMakeLists.txt is near the 256 KiB cap, tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_health.c rd_health_live.c)

# rd_health_test: the rule (CPU) over the four logs' patterns: issue 179's
# Mali-G52 v0.11.6 runs meet it at replay 164, once; its v0.11.5 run, the
# Adreno 740's isolated hangs and restarts, an idle GPU's restarts and
# unjudged pictures never do; 15 of 32 never, 16 of 32 does
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
