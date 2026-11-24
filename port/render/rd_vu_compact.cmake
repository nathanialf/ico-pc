# rd_vu_compact.cmake: the index compaction (rd_vu_compact.c, rd_vu_compact.h;
# port/shaders/vu_tri.hlsl vu_tri_keep_cs; docs/port/PIPELINES.md "The index
# compaction"), included at the end of port/render/CMakeLists.txt (a file of
# its own: port/render/CMakeLists.txt is near the 256 KiB cap,
# tools/check_no_rom.sh).
# rd_vu_compact_selftest.c: the Mali-G52 form's own start-up test
target_sources(ico_render PRIVATE rd_vu_compact.c rd_vu_compact_selftest.c)

# rd_vu_compact_test: the pure parts (CPU): the plan's places, workgroups,
# raises and refusals (an empty list, the plane's room, the device's largest
# index value raised and not), the counts summed by list, the counts' base,
# the window's line word for word and cut short, the tests' switch's words;
# the Mali-G52 form's lists written by the CPU, its own test's read-back
# check (as written, filler, mixed, missing, wrong, past the count, counts
# not the triangles, against the geometry) and its words, the window's
# line's CPU clause
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
#   rd_vuprepass_selftest_compact  lavapipe is another device (off by
#                      default: measured on the Mali-G52 only), so the tests'
#                      ICO_RD_VU_COMPACT=1: passes with it, the verdict names
#                      the copy, the compaction's line says it passed
#                      (attempt_ledger_test: a self-test that never ran keeps
#                      it off but under the switch)
#   rd_vuprepass_selftest_compact_refused  every triangle written as a filler
#                      (ICO_RD_VU_COMPACT_SABOTAGE=1, under ICO_RD_VU_COMPACT=1):
#                      the run with it fails, the
#                      run without it passes, the compaction refused for the
#                      run, the pre-pass on with the record by compute
#   rd_vuprepass_selftest_compact_mali  under the phone's limits
#                      (ICO_VK_FAKE_LIMITS=mali, ICO_VK_FAKE_VENDOR=0x13B5 for
#                      the tile rule, ICO_VK_FAKE_NO_DYNAMIC=1: every state in
#                      the pipeline keys, as on the phone) and the Mali-G52's
#                      class by the tests' switch (ICO_RD_VU_COMPACT_ARM=1:
#                      RhiLimits.vendorId and the device name stay
#                      lavapipe's), synchronisation validation with the
#                      submit-time checks: the class's form since v0.11.16
#                      (rd_vu_compact.h: the CPU writes the lists first, the
#                      GPU only the fillers), so the record's test runs
#                      without it (no copy in its frame) and its own test
#                      (rd_vu_compact_selftest.c) passes: 6 replays at the
#                      play's record size, 193536 triangles read back as
#                      written or as fillers where the GPU counted them, the
#                      pictures byte for byte, every stamp written; on after
#                      it, the mark "counting v0.0.0"
#   _mali_lists        the CPU's lists written one triangle off
#                      (ICO_RD_VU_COMPACT_SABOTAGE=lists: a list other than
#                      the draw's, its values in range, as a stale plane
#                      would hold): refused by the lists' check (values neither
#                      the CPU's nor a filler, in replay 1's centre draw),
#                      off, the ledger's refusal, the mark "failed v0.0.0"
#   _mali_readback     one filler of the test's read-back taken back to its
#                      corner 0 (=readback: a filler store lost in part, the
#                      report's third cause): refused by the lists' check
#                      (1 mixed, 1 filler write missing, replay 1's draw past
#                      the edge, triangle 0), the mark "failed v0.0.0"
#   _mali_stamps       every record of the test's own pictures read as a
#                      hang (ICO_RD_HEALTH_FAKE=selftest: v0.11.6's sign on
#                      the phone): refused by the stamps with the lists and
#                      the pictures right, the mark "failed v0.0.0"
#   _mali_marked       kept off by its install's mark (ICO_COMPACT_BLOCK, as
#                      compact_mark.c exports it after a bad end while it
#                      was tried): off with the mark's words, no own test,
#                      no copy in the record's frame
#   rd_vuprepass_selftest_compact_stores  the vertex stores (lavapipe's own
#                      path): not applicable, no copy in the frame
if(ICO_RUN_TESTS AND TARGET rd_vuprepass_selftest_test AND DEFINED _vst)
    set(_cmp_copy ", the index compaction \\(a copy past the target's edge, its lists from value [0-9]+ of the fourth plane, the record at [0-9]+ rows, no stamp of the test's [0-9]+ pictures unwritten\\) and particles")
    set(_cmp_rec "ms, record compute")
    add_test(NAME rd_vuprepass_selftest_compact
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: VU index compaction: on by the tests' switch: .*rd: VU index compaction: on: passed its start-up self-test with the record's \\(ICO_RD_VU_COMPACT=1, its start-up self-test passed, not the Mali-G52 or the PowerVR D-Series DXT-48-1536 \\([^\n]*, vendor 0x[0-9A-F]+\\): off by default\\).*rd: VU pre-pass: on \\(self-test: [^\n]*${_cmp_copy}[^\n]*${_cmp_rec}"
                     -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_compact_refused
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: VU index compaction: ICO_RD_VU_COMPACT_SABOTAGE: .*rd: VU pre-pass: the record with the index compaction failed the self-test \\(the pictures differ: .*rd: VU index compaction: refused by its start-up self-test \\(the record's test with it: the pictures differ: .*rd: VU pre-pass: on \\(self-test: [^\n]*${_cmp_rec}"
                     "-DNOT_=${_cmp_copy}" -P "${_vst}")
    # the Mali-G52's form (RD_VU_COMPACT_ARM_DEFAULT 1 since v0.11.16)
    set(_cmp_g52 "the Mali-G52 \\([^\n]*, vendor 0x[0-9A-F]+, ICO_RD_VU_COMPACT_ARM=1\\): RD_VU_COMPACT_ARM_DEFAULT 1")
    set(_cmp_try "rd: VU index compaction: to be tried by its own start-up test, on where it passes: [^\n]*the Mali-G52's form: every planned list written whole by the CPU first, only the fillers by the GPU \\(its start-up self-test not run \\(yet\\); ${_cmp_g52}\\)")
    set(_cmp_own "rd: VU index compaction: its own start-up test passed \\(the Mali-G52's form: 6 replays in both frame slots on the record at [0-9]+ rows, 4 draws of 8064 triangles a replay, the lists from value [0-9]+ moved 1237 values a replay; the lists read back: 193536 triangles read back: [1-9][0-9]* as the CPU wrote them, [1-9][0-9]* fillers where the GPU counted [1-9][0-9]* left out; the pictures byte for byte with it off, [1-9][0-9]* of [0-9]+ pixels drawn; no stamp of the test's [1-9][0-9]* pictures unwritten\\)")
    set(_cmp_ownoff "rd: VU index compaction: off after the start-up self-tests \\(its own start-up test refused it \\(the lists the CPU writes first read back, the pictures or the stamps\\): every draw reads its own index list; ${_cmp_g52}\\)")
    set(_cmp_mark "test: the index compaction's mark after the self-test: ")
    set(_cmp_markenv "ICO_COMPACT_VERSION=v0.0.0;ICO_COMPACT_MARK=${CMAKE_CURRENT_BINARY_DIR}/compact_mark")
    add_test(NAME rd_vuprepass_selftest_compact_mali
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=${_cmp_try}.*${_cmp_own}.*rd: VU index compaction: on: passed its own start-up test \\(the Mali-G52, its own start-up test passed \\(the lists written by the CPU first\\); ${_cmp_g52}\\).*rd: VU pre-pass: on \\(self-test: [^\n]*${_cmp_rec}.*${_cmp_mark}counting v0.0.0"
                     "-DNOT_=${_cmp_copy}|refused by its own start-up test|not checked by its own start-up test" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_compact_mali_lists
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: VU index compaction: ICO_RD_VU_COMPACT_SABOTAGE: the lists the CPU writes first written one triangle off .*rd: VU index compaction: refused by its own start-up test \\(the lists: [^\n]*with a value neither the CPU's nor a filler \\(a stale or lost list\\)[^\n]* in replay 1 of 6 \\(frame slot [0-9]+\\), draw 1 \\(the centre\\), the first at triangle [0-9]+: a list other than the one written read back [^\n]*: off for the run, its mark says failed.*${_cmp_ownoff}.*rd: VU pre-pass: on \\(self-test: [^\n]*${_cmp_rec}.*${_cmp_mark}failed v0.0.0"
                     "-DNOT_=${_cmp_copy}|its own start-up test passed" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_compact_mali_readback
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: VU index compaction: ICO_RD_VU_COMPACT_SABOTAGE: one filler of its own test's lists read back as its corner 0 .*rd: VU index compaction: refused by its own start-up test \\(the lists: [^\n]*; 1 mixed \\(a filler store lost in part\\); 1 filler writes missing \\(an unwritten filler leaves its triangle as it was\\) in replay 1 of 6 \\(frame slot [0-9]+\\), draw 3 \\(past the edge\\), the first at triangle 0: the keep stage's stores lost, the report's third cause[^\n]*: off for the run, its mark says failed.*${_cmp_ownoff}.*${_cmp_mark}failed v0.0.0"
                     "-DNOT_=${_cmp_copy}|its own start-up test passed" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_compact_mali_stamps
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: picture health: ICO_RD_HEALTH_FAKE=selftest: .*rd: VU index compaction: refused by its own start-up test \\(the GPU's own sign of a reset in [1-9][0-9]* of the test's [1-9][0-9]* pictures \\(recorded stamps unwritten after the work was signalled finished, or the clock restarted with work queued: v0.11.6's sign on the Mali-G52\\); the lists and the pictures right\\)[^\n]*: off for the run, its mark says failed.*${_cmp_ownoff}.*${_cmp_mark}failed v0.0.0"
                     "-DNOT_=${_cmp_copy}|its own start-up test passed" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_compact_mali_marked
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: VU index compaction: off on this install \\(was being tried when it ended \\(a crash\\): off on this install until the version changes \\(blocked v0.0.0\\)\\).*rd: VU index compaction: off \\(kept off on this install by its mark \\(the run before ended badly while it was tried or on\\); ${_cmp_g52}\\)"
                     "-DNOT_=${_cmp_copy}|its own start-up test|to be tried" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_compact_stores
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: VU index compaction: off \\(record path stores: vertex shader stores"
                     "-DNOT_=${_cmp_copy}" -P "${_vst}")
    set(_cmp_tests rd_vuprepass_selftest_compact rd_vuprepass_selftest_compact_refused
                   rd_vuprepass_selftest_compact_mali rd_vuprepass_selftest_compact_stores
                   rd_vuprepass_selftest_compact_mali_lists
                   rd_vuprepass_selftest_compact_mali_readback
                   rd_vuprepass_selftest_compact_mali_stamps
                   rd_vuprepass_selftest_compact_mali_marked)
    set_tests_properties(${_cmp_tests} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
    set_tests_properties(rd_vuprepass_selftest_compact
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_RECORD=compute;ICO_RD_VU_COMPACT=1")
    set_tests_properties(
        rd_vuprepass_selftest_compact_refused
        PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_RECORD=compute;ICO_RD_VU_COMPACT=1;ICO_RD_VU_COMPACT_SABOTAGE=1")
    if(DEFINED _env_mali_pre)
        set(_cmp_mali "${_env_mali_pre};ICO_VK_FAKE_VENDOR=0x13B5;ICO_RD_VU_COMPACT_ARM=1;ICO_RD_VU_RECORD=compute;ICO_VK_FAKE_NO_DYNAMIC=1")
    else()
        set(_cmp_mali "${_env};ICO_VK_FAKE_LIMITS=mali;ICO_VK_FAKE_VENDOR=0x13B5;ICO_RD_VU_COMPACT_ARM=1;ICO_RD_VU_RECORD=compute;ICO_VK_FAKE_NO_DYNAMIC=1")
    endif()
    set_tests_properties(rd_vuprepass_selftest_compact_mali
                         PROPERTIES ENVIRONMENT "${_cmp_mali};${_cmp_markenv}_on.txt")
    set_tests_properties(
        rd_vuprepass_selftest_compact_mali_lists
        PROPERTIES ENVIRONMENT "${_cmp_mali};ICO_RD_VU_COMPACT_SABOTAGE=lists;${_cmp_markenv}_lists.txt")
    set_tests_properties(
        rd_vuprepass_selftest_compact_mali_readback
        PROPERTIES ENVIRONMENT
                   "${_cmp_mali};ICO_RD_VU_COMPACT_SABOTAGE=readback;${_cmp_markenv}_readback.txt")
    set_tests_properties(
        rd_vuprepass_selftest_compact_mali_stamps
        PROPERTIES ENVIRONMENT "${_cmp_mali};ICO_RD_HEALTH_FAKE=selftest;${_cmp_markenv}_stamps.txt")
    set_tests_properties(
        rd_vuprepass_selftest_compact_mali_marked
        PROPERTIES ENVIRONMENT
                   "${_cmp_mali};ICO_COMPACT_BLOCK=was being tried when it ended (a crash): off on this install until the version changes (blocked v0.0.0);${_cmp_markenv}_marked.txt")
    set_tests_properties(rd_vuprepass_selftest_compact_stores
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_RECORD=stores")
endif()

# rd_vu_compact_ab (test/rd_vu_compact_ab.cmake): every fixture of
# test/fixtures/lists/vu_flat0.txt (committed frames, rd_fixture.cmake,
# docs/port/TESTING_FIXTURES.md; a listed fixture that is absent fails: a
# Mac reporter's 16:9 frame, issue 179's Mali-G52 frames under the Arm fakes
# at its settings, the Pixel's three opening frames at its setting) with the
# record by compute dispatches, the compaction on (ICO_RD_VU_COMPACT=1)
# against off (=0): every target byte for byte, merged and one draw a
# command, MSAA and a fractional scale among the corpus settings; every
# pair's counts matched against its line of
# test/fixtures/lists/vu_compact_counts.txt, the triangles left out and the
# camera rule's draws each fixture declares (the path ran).  The drop
# count D is also the triangles vu_main_vs returns early on (vu_tri.hlsl
# ICO_VU_EARLY): vu_keep_position takes its culled from the same helper,
# vu_tri_verdict, so D unchanged on fx-mali-frame (4612 of list 0's
# 12192), fx-mali-slow541 (8196 of 19185) and fx-opening-sun (138) holds
# the verdict as it was and shows the early return ran there.
# rd_vu_compact_ab_pixel: the same on the frames rd_pixel leaves (a list of
# build-tree names, the counts printed).  No device: SKIP.
include(rd_fixture.cmake)
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vu_compact_ab"
         "${CMAKE_CURRENT_BINARY_DIR}/vu_compact_ab_pixel")
    add_test(NAME rd_vu_compact_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     -DFIXTURE_TOOL=$<TARGET_FILE:rd_fixture_tool>
                     "-DFIXTURES=${ICO_FIXTURE_DIR}" "-DFIXTURE_HELPERS=${ICO_FIXTURE_HELPERS}"
                     "-DLIST=${ICO_FIXTURE_DIR}/lists/vu_flat0.txt"
                     "-DCOUNTS=${ICO_FIXTURE_DIR}/lists/vu_compact_counts.txt"
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
