# rd_vu_compute_tests.cmake: the VU record written by compute dispatches
# (rd_vu_record.h; issue 179: the Mali-G52 MC2 offers no
# vertexPipelineStoresAndAtomics), included from port/render/CMakeLists.txt
# inside if(ICO_RUN_TESTS), after the self-test's rows (_vst, _vst_on,
# _vst_stale, _env, _env_mali_pre) and rd_vurecord_ab (the rd_dump fixture).
# A file of its own: port/render/CMakeLists.txt is within a few KB of the
# 256 KiB cap (tools/check_no_rom.sh).
#
# lavapipe offers both the vertex stores and the compute stores, so auto
# takes the stores and these rows ask for the compute dispatches by name or
# fake the stores away:
#   rd_pixel_compute   rd_pixel's pictures and hashes with the record by
#                      compute dispatches (ICO_RD_VU_RECORD=compute), byte for
#                      byte the fixture's, under synchronisation validation
#   rd_vuprepass_selftest_compute, _compute_plain  the self-test passes and
#                      the verdict names "record compute"
#   _compute_sabotage_straddle  a stale record (replays 2 on store into the
#                      sink) fails the compute run from replay 2 on, logged
#                      with its first texel, and the targets' run after it
#                      (the final verdict names the failed rung)
#   _nostores_fallback (in CMakeLists.txt, updated): a device without vertex
#                      stores (ICO_VK_FAKE_NO_VERTEX_STORES=1) takes the
#                      compute dispatches: the start line's words
#   _nocompute_fallback  neither (both fakes): the targets, both reasons on
#                      the start line
#   _compute_mali      the compute run under the phone's limits
#                      (ICO_VK_FAKE_LIMITS=mali: four storage buffers a stage,
#                      the compute stage's three) with the submit-time checks
#   _compute_vk11      through a Vulkan 1.1 device without dynamic rendering or
#                      dynamic state (the dispatches outside the legacy passes)
#   rd_vurecord_ab_compute  rd_vurecord_ab with A the render targets and B the
#                      compute dispatches: every named target byte for byte,
#                      B's stats naming the write and counting dispatches
#   rd_vuprepass_selftest_sink_fail  the store passes' targets refused: the
#                      stores off for the run, the compute dispatches (below)
set(_vst_compute_line "rd: VU pre-pass: on \\(self-test: [^\n]* ms, record compute")
set(_cd "${CMAKE_CURRENT_BINARY_DIR}/vu_record_compute")
file(MAKE_DIRECTORY "${_cd}")
add_test(NAME rd_pixel_compute COMMAND rd_pixel_test "${_cd}")
set_tests_properties(rd_pixel_compute PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_cd}"
                                                 ENVIRONMENT "${_env};ICO_RD_VU_RECORD=compute")
add_test(NAME rd_vuprepass_selftest_compute
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                 "-DLINE=${_vst_on}" "-DEXTRA=${_vst_compute_line}" -P "${_vst}")
add_test(NAME rd_vuprepass_selftest_compute_plain
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                 -DVARIANT=plain "-DLINE=${_vst_on}" "-DEXTRA=${_vst_compute_line}" -P "${_vst}")
add_test(NAME rd_vuprepass_selftest_compute_sabotage_straddle
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=off
                 "-DLINE=${_vst_stale}"
                 "-DEXTRA=rd: VU pre-pass: the record stored by compute dispatches failed the self-test \\(the pictures differ: in replay [2-4] of 4.*, record targets, the compute failed the self-test"
                 -P "${_vst}")
add_test(NAME rd_vuprepass_selftest_nocompute_fallback
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                 "-DLINE=${_vst_on}"
                 "-DEXTRA=the record drawn into 3 RGBA32_UINT render targets and copied into the frame slot's record buffer \\(no vertex stores: the device has no vertexPipelineStoresAndAtomics \\(or ICO_VK_FAKE_NO_VERTEX_STORES\\), no compute stores: ICO_VK_FAKE_NO_COMPUTE_STORES\\).*, record targets"
                 -P "${_vst}")
add_test(NAME rd_vuprepass_selftest_compute_mali
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                 "-DLINE=${_vst_on}" "-DEXTRA=${_vst_compute_line}" -P "${_vst}")
add_test(NAME rd_vuprepass_selftest_compute_vk11
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                 "-DLINE=${_vst_on}" "-DEXTRA=${_vst_compute_line}" -P "${_vst}")
set(_vst_compute_tests
    rd_vuprepass_selftest_compute rd_vuprepass_selftest_compute_plain
    rd_vuprepass_selftest_compute_sabotage_straddle rd_vuprepass_selftest_nocompute_fallback
    rd_vuprepass_selftest_compute_mali rd_vuprepass_selftest_compute_vk11)
set_tests_properties(${_vst_compute_tests} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
set_tests_properties(rd_vuprepass_selftest_compute rd_vuprepass_selftest_compute_plain
                     PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_RECORD=compute")
set_tests_properties(rd_vuprepass_selftest_compute_sabotage_straddle
                     PROPERTIES ENVIRONMENT
                                "${_env};ICO_RD_VU_RECORD=compute;ICO_RD_VU_PREPASS_SABOTAGE=stale")
set_tests_properties(
    rd_vuprepass_selftest_nocompute_fallback
    PROPERTIES ENVIRONMENT
               "${_env};ICO_VK_FAKE_NO_VERTEX_STORES=1;ICO_VK_FAKE_NO_COMPUTE_STORES=1")
set_tests_properties(rd_vuprepass_selftest_compute_mali
                     PROPERTIES ENVIRONMENT "${_env_mali_pre};ICO_RD_VU_RECORD=compute")
set_tests_properties(
    rd_vuprepass_selftest_compute_vk11
    PROPERTIES ENVIRONMENT
               "${_env_mali_pre};ICO_RD_VU_RECORD=compute;ICO_VK_FAKE_API=1.1;ICO_VK_FAKE_NO_DYNAMIC=1")
add_test(NAME rd_vurecord_ab_compute
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                 -DNAME=rd_vurecord_ab_compute
                 "-DDUMP=${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_frame.rddump"
                 "-DLIST=${CMAKE_SOURCE_DIR}/tools/vustage_dumps.txt"
                 "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vurecord_ab_compute"
                 "-DENV_A=ICO_RD_VU_RECORD=targets" "-DENV_B=ICO_RD_VU_RECORD=compute"
                 "-DREQ_A=0 mesh and grid draws the old way.*stats: VU record targets: drawn into 3"
                 "-DREQ_B=copies, [0-9]+ dispatches.*0 mesh and grid draws the old way.*stats: VU record compute: stored by compute dispatches"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_env_ab.cmake")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vurecord_ab_compute")
set_tests_properties(rd_vurecord_ab_compute PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 3600
                                                       FIXTURES_REQUIRED rd_dump)
if(_env)
    set_tests_properties(rd_vurecord_ab_compute PROPERTIES ENVIRONMENT "${_env}")
endif()
# rd_vuprepass_selftest_sink_fail: the store passes' two throwaway targets
# refused (ICO_RD_VU_REC_SINK_FAIL=1, rd_vu_rec_sink.h) under the stores asked
# for: a store pass without them would begin with no attachment while its
# pipelines name one (invalid usage), so the stores are off for the run before
# the self-test (rd_vu_record.c rd__vu_record_shaders) and the record takes the
# compute dispatches: both lines and the self-test's compute verdict, under
# synchronisation validation
add_test(NAME rd_vuprepass_selftest_sink_fail
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                 "-DLINE=${_vst_on}"
                 "-DEXTRA=the store passes' two throwaway 1024x1 R8 targets could not be made: the stores off for the run.*rd: VU record: stored by compute dispatches[^\n]*\\(the stores off: the store passes' throwaway targets could not be made\\).*, record compute"
                 -P "${_vst}")
set_tests_properties(rd_vuprepass_selftest_sink_fail
                     PROPERTIES ENVIRONMENT
                                "${_env};ICO_RD_VU_RECORD=stores;ICO_RD_VU_REC_SINK_FAIL=1")
# rd_scope_ab, rd_scope_ab_tiler (test/rd_vu_env_ab.cmake; issues 179 and
# 211, port/rhi/rhi_tile_rule.h): the same dumps under v0.11.3's narrow
# barrier scopes (ICO_VK_FAKE_SCOPES=narrow: the texture uploads after the
# VU pre-pass) and v0.11.2's wide ones with the uploads before it (the
# tile-based phones' default since v0.11.4): every named target byte for
# byte.  rd_scope_ab: the wide run by Arm's vendor id faked
# (ICO_VK_FAKE_VENDOR=0x13B5: the tile rule), the desktop profile;
# rd_scope_ab_tiler: both runs on a tile-based profile
# (ICO_VK_FAKE_LIMITS=moltenvk, rd_profile.c's Apple class), the wide run by
# the tile rule, the narrow one faked.  The dumps: rd_pixel's, and tools/scope_dumps.txt's
# (the VU stage list's and issue 179's two loss frames, machine-local)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/scope_ab" "${CMAKE_CURRENT_BINARY_DIR}/scope_ab_tiler")
add_test(NAME rd_scope_ab
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> -DNAME=rd_scope_ab
                 "-DDUMP=${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_frame.rddump"
                 "-DLIST=${CMAKE_SOURCE_DIR}/tools/scope_dumps.txt"
                 "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/scope_ab"
                 "-DENV_A=ICO_VK_FAKE_SCOPES=narrow" "-DENV_B=ICO_VK_FAKE_VENDOR=0x13B5"
                 "-DREQ_A=stats: [0-9]+ draws, [0-9]+ passes" "-DREQ_B=stats: [0-9]+ draws, [0-9]+ passes"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_env_ab.cmake")
add_test(NAME rd_scope_ab_tiler
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> -DNAME=rd_scope_ab_tiler
                 "-DDUMP=${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_frame.rddump"
                 "-DLIST=${CMAKE_SOURCE_DIR}/tools/scope_dumps.txt"
                 "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/scope_ab_tiler"
                 "-DENV_A=ICO_VK_FAKE_LIMITS=moltenvk|ICO_VK_FAKE_SCOPES=narrow"
                 "-DENV_B=ICO_VK_FAKE_LIMITS=moltenvk"
                 "-DREQ_A=stats: [0-9]+ draws, [0-9]+ passes" "-DREQ_B=stats: [0-9]+ draws, [0-9]+ passes"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_env_ab.cmake")
set_tests_properties(rd_scope_ab rd_scope_ab_tiler PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                   TIMEOUT 3600 FIXTURES_REQUIRED rd_dump)
if(_env)
    set_tests_properties(rd_scope_ab rd_scope_ab_tiler PROPERTIES ENVIRONMENT "${_env}")
endif()
