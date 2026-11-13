# rd_vu_compact.cmake: the index compaction (rd_vu_compact.c, rd_vu_compact.h;
# port/shaders/vu_tri.hlsl vu_tri_keep_cs; docs/port/PIPELINES.md "The index
# compaction"), included at the end of port/render/CMakeLists.txt (a file of
# its own: port/render/CMakeLists.txt is near the 256 KiB cap,
# tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_vu_compact.c)

# rd_vu_compact_test: the pure parts (CPU): the plan's places, workgroups,
# raises and refusals (an empty list, the plane's room, the device's largest
# index value raised and not), the counts summed by list, the counts' base,
# the window's line word for word and cut short, the tests' switch's words
add_executable(rd_vu_compact_test test/rd_vu_compact_test.c)
target_compile_options(rd_vu_compact_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_vu_compact_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                      "${CMAKE_SOURCE_DIR}/port/rhi"
                                                      "${CMAKE_SOURCE_DIR}/port/shaders"
                                                      "${CMAKE_SOURCE_DIR}/port/platform")
target_link_libraries(rd_vu_compact_test PRIVATE ico_render)
if(ICO_RUN_TESTS)
    add_test(NAME rd_vu_compact COMMAND rd_vu_compact_test)
endif()

# The start-up self-test with the compaction (rd_vuprepass_selftest.c: the
# record's four replays with it, the frame with a copy of the prelit mesh past
# the target's right edge; lavapipe takes the vertex stores unless asked, so
# the rows ask for the compute record):
#   rd_vuprepass_selftest_compact  passes with it: the verdict names the copy,
#                      the compaction's line says it passed
#   rd_vuprepass_selftest_compact_refused  every triangle written as a filler
#                      (ICO_RD_VU_COMPACT_SABOTAGE=1): the run with it fails, the
#                      run without it passes, the compaction refused for the
#                      run, the pre-pass on with the record by compute
#   rd_vuprepass_selftest_compact_mali  under the phone's limits
#                      (ICO_VK_FAKE_LIMITS=mali, ICO_VK_FAKE_VENDOR=0x13B5 for
#                      the tile rule) and the Arm class by the tests' switch
#                      (ICO_RD_VU_COMPACT_ARM=1: RhiLimits.vendorId stays
#                      lavapipe's), synchronisation validation with the
#                      submit-time checks: passes, its lines naming the Arm
#                      class and RD_VU_COMPACT_ARM_DEFAULT
#   rd_vuprepass_selftest_compact_stores  the vertex stores (lavapipe's own
#                      path): not applicable, no copy in the frame
if(ICO_RUN_TESTS AND TARGET rd_vuprepass_selftest_test AND DEFINED _vst)
    set(_cmp_copy ", the index compaction \\(a copy past the target's edge\\) and particles")
    set(_cmp_rec "ms, record compute")
    add_test(NAME rd_vuprepass_selftest_compact
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: VU index compaction: on: passed its start-up self-test with the record's \\(its start-up self-test passed, not the Arm class \\(vendor 0x[0-9A-F]+\\): RD_VU_COMPACT_DEFAULT 1\\).*rd: VU pre-pass: on \\(self-test: [^\n]*${_cmp_copy}[^\n]*${_cmp_rec}"
                     -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_compact_refused
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: VU index compaction: ICO_RD_VU_COMPACT_SABOTAGE: .*rd: VU pre-pass: the record with the index compaction failed the self-test \\(the pictures differ: .*rd: VU index compaction: refused by its start-up self-test \\(the record's test with it: the pictures differ: .*rd: VU pre-pass: on \\(self-test: [^\n]*${_cmp_rec}"
                     "-DNOT_=${_cmp_copy}" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_compact_mali
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: VU index compaction: on: passed its start-up self-test with the record's \\(the Arm class, its start-up self-test passed, the Arm class \\(vendor 0x[0-9A-F]+, ICO_RD_VU_COMPACT_ARM=1\\): RD_VU_COMPACT_ARM_DEFAULT 1\\).*${_cmp_copy}"
                     -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_compact_stores
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: VU index compaction: off \\(record path stores: vertex shader stores"
                     "-DNOT_=${_cmp_copy}" -P "${_vst}")
    set(_cmp_tests rd_vuprepass_selftest_compact rd_vuprepass_selftest_compact_refused
                   rd_vuprepass_selftest_compact_mali rd_vuprepass_selftest_compact_stores)
    set_tests_properties(${_cmp_tests} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
    set_tests_properties(rd_vuprepass_selftest_compact
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_RECORD=compute")
    set_tests_properties(
        rd_vuprepass_selftest_compact_refused
        PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_RECORD=compute;ICO_RD_VU_COMPACT_SABOTAGE=1")
    if(DEFINED _env_mali_pre)
        set_tests_properties(
            rd_vuprepass_selftest_compact_mali
            PROPERTIES ENVIRONMENT
                       "${_env_mali_pre};ICO_VK_FAKE_VENDOR=0x13B5;ICO_RD_VU_COMPACT_ARM=1;ICO_RD_VU_RECORD=compute")
    else()
        set_tests_properties(
            rd_vuprepass_selftest_compact_mali
            PROPERTIES ENVIRONMENT
                       "${_env};ICO_VK_FAKE_LIMITS=mali;ICO_VK_FAKE_VENDOR=0x13B5;ICO_RD_VU_COMPACT_ARM=1;ICO_RD_VU_RECORD=compute")
    endif()
    set_tests_properties(rd_vuprepass_selftest_compact_stores
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_RECORD=stores")
endif()

# rd_vu_compact_ab (test/rd_vu_compact_ab.cmake): every dump of the
# machine-local tools/vu_flat0_dumps.txt (the 2026-10-07 captures, three
# reporters' frames, issue 179's four Mali-G52 dumps under the Arm fakes at
# its settings, the Pixel's three opening dumps at its setting; a listed dump
# that is absent fails where /primary/dev/ico-dumps exists) with the record by
# compute dispatches, the compaction on (ICO_RD_VU_COMPACT=1) against off
# (=0): every target byte for byte, merged and one draw a command, MSAA and a
# fractional scale among the corpus settings; the counts printed and, where
# tools/vu_compact_counts.txt has a line, matched.  rd_vu_compact_ab_pixel:
# the same on the frames rd_pixel leaves.  No device: SKIP.
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vu_compact_ab"
         "${CMAKE_CURRENT_BINARY_DIR}/vu_compact_ab_pixel")
    add_test(NAME rd_vu_compact_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/vu_flat0_dumps.txt"
                     "-DCOUNTS=${CMAKE_SOURCE_DIR}/tools/vu_compact_counts.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_compact_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_compact_ab.cmake")
    set_tests_properties(rd_vu_compact_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                         TIMEOUT 7200)
    set(_cmp_list "${CMAKE_CURRENT_BINARY_DIR}/rd_vu_compact_pixel_dumps.txt")
    file(WRITE "${_cmp_list}"
         "corpus rd_pixel_vu.rddump\ncorpus rd_pixel_vu36.rddump\ncorpus rd_pixel_frame.rddump\n")
    add_test(NAME rd_vu_compact_ab_pixel
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> "-DLIST=${_cmp_list}"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_compact_ab_pixel"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_compact_ab.cmake")
    set_tests_properties(rd_vu_compact_ab_pixel PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                         FIXTURES_REQUIRED rd_dump TIMEOUT 600)
endif()
