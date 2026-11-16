# Included from port/render/CMakeLists.txt inside the tests' scope (_env,
# _vu_fx, _vskd, _vskd_clean in scope): the skinned draws' checks on the
# committed fixtures (docs/port/TESTING_FIXTURES.md; issue 163 and issue
# 123's ladder, docs/port/VU_SKIN_CHECK.md).  Its own file because
# port/render/CMakeLists.txt is near the 256 KiB cap (tools/check_no_rom.sh).
#
# rd_vu_skin_check_dump, rd_vu_skin_check_163_probe, _163_ring and the
# rd_vu_skin_check_123* rows (test/rd_vu_skin_check_dump.cmake):
# rd_replay_tool --skin-check on the fixture of
# test/fixtures/lists/skin_check.txt whose line holds the row's word
# (corpus: fx-vu-3086, 46 skinned commands; 163: issue 163's blended
# frame; 123: issue 123's v0.10.4 blended frame), the summary line matched
# and each REQUIRE quantity held as declared (the path ran).  The fixtures
# keep the poses and generate the skinned vertices (half on two bones,
# 16 + 4 (s mod n) and the next), which reach address 200 on both reporter
# frames, so the tests' faults are put there as before by
# ICO_RD_VU_SKIN_SABOTAGE: the bone at 200 read as zeros by the probe
# (H1, address-dependent), zeroed in the ring after the bytes were kept
# (H2), made NaN in the ring (the v0.10.4 picture's form: the wrong
# vertices dropped).  The mismatch line names the fixture's generated mesh
# (m0001 ...), not the game's.  SKIP without a device.
set(_vsk_fx ${_vu_fx} "-DLIST=${ICO_FIXTURE_DIR}/lists/skin_check.txt")
# a wrong check's mismatch line at bone address 200 (review2 S5: the H2
# run's 42 changed blocks must not use up its lines)
set(_vsk_wrong "rd: VU skin check: frame [0-9]+ command 0:[0-9]+ mesh m[0-9]+ #[0-9]+ \\([0-9]+ vertices\\): [0-9]+ vertices wrong of [0-9]+, address 200: ")
add_test(NAME rd_vu_skin_check_dump
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> ${_vsk_fx} -DROLE=corpus
                 -DREQUIRE=skin_checks "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_dump"
                 "-DLINE=46 skinned commands, [1-9][0-9]* checks, 0 wrong \\(address 0\\), 0 blocks whose bytes differed \\(0 changed after the draw\\), 0 of [0-9]+ box checks with samples outside"
                 -P "${_vskd}")
add_test(NAME rd_vu_skin_check_163_probe
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> ${_vsk_fx} -DROLE=163
                 "-DREQUIRE=skin_checks|skin_wrong"
                 "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_163_probe"
                 "-DLINE=[0-9]+ skinned commands, [0-9]+ checks, [1-9][0-9]* wrong \\(address 200\\), 0 blocks whose bytes differed \\(0 changed after the draw\\).*reads as: H1, address-dependent"
                 "-DLOG=${_vsk_wrong}" -P "${_vskd}")
add_test(NAME rd_vu_skin_check_163_ring
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> ${_vsk_fx} -DROLE=163
                 "-DREQUIRE=skin_checks|skin_wrong|skin_changed"
                 "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_163_ring"
                 "-DLINE=[0-9]+ skinned commands, [0-9]+ checks, [1-9][0-9]* wrong \\(address 200\\), [1-9][0-9]* blocks whose bytes differed \\([1-9][0-9]* changed after the draw\\).*reads as: H2, a port race"
                 "-DLOG=${_vsk_wrong}" -P "${_vskd}")
# rd_vu_skin_check_123*: issue 123's v0.10.4 blended frame (Yorda's shin
# missing on the device): clean (the twins equal), and with the bone at 200
# made NaN in the ring after its bytes were kept (the device's picture's
# form, B0): wrong at address 200 with the wrong vertices dropped, H2, the
# twins differing
set(_vsk123 ${_vsk_fx} -DROLE=123)
add_test(NAME rd_vu_skin_check_123
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> ${_vsk123}
                 -DREQUIRE=skin_checks "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_123"
                 "-DLINE=${_vskd_clean}.*twins: [1-9][0-9]* read \\([0-9]+ through the draw's own pipeline\\), 0 differ"
                 -P "${_vskd}")
add_test(NAME rd_vu_skin_check_123_nan
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> ${_vsk123}
                 "-DREQUIRE=skin_checks|skin_wrong|skin_changed|skin_dropped"
                 "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_123_nan"
                 "-DLINE=[0-9]+ skinned commands, [0-9]+ checks, [1-9][0-9]* wrong \\(address 200\\).*reads as: H2, a port race.*twins: [1-9][0-9]* read \\([0-9]+ through the draw's own pipeline\\), [1-9][0-9]* differ.*[1-9][0-9]* wrong vertices dropped"
                 "-DLOG=${_vsk_wrong}.*reads as non-finite / dropped" -P "${_vskd}")
# _attach: the same two under the Turnip reporter's profile on lavapipe
# (the attachment read, GS state as shader data, the VU pre-pass off as on
# a tile-based GPU), so the draws whose first pass reads the attachment
# with Z writes off are twinned through their own pipelines with the write
# mask cleared: clean every twin equal with at least one through the
# draw's own pipeline; with the NaN bone the same and the twins differing
# (the fixture's blended VU textures are clear, skin_twins.rules.txt: those
# draws' one pass fails the alpha test and writes no Z, so both kinds of
# twin are drawn; each event line says which)
set(_vsk_attach "ICO_RD_VU_PREPASS=0;ICO_VK_FAKE_NO_INTERLOCK=1;ICO_RD_BLEND=attachment;ICO_RD_UBER=on")
add_test(NAME rd_vu_skin_check_123_attach
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> ${_vsk123}
                 "-DREQUIRE=skin_checks|skin_twins_own"
                 "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_123_attach"
                 "-DLINE=${_vskd_clean}.*twins: [1-9][0-9]* read \\([1-9][0-9]* through the draw's own pipeline\\), 0 differ"
                 -P "${_vskd}")
add_test(NAME rd_vu_skin_check_123_nan_attach
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> ${_vsk123}
                 "-DREQUIRE=skin_checks|skin_wrong|skin_dropped|skin_twins_own"
                 "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_123_nan_attach"
                 "-DLINE=[0-9]+ skinned commands, [0-9]+ checks, [1-9][0-9]* wrong \\(address 200\\).*twins: [1-9][0-9]* read \\([1-9][0-9]* through the draw's own pipeline\\), [1-9][0-9]* differ"
                 "-DLOG=twins differ: A \\(the draw's offset, page phase [0-9]+\\) [0-9]+ samples, B \\(fresh, phase [0-9]+\\) [0-9]+ samples(, C \\(ring read\\) [0-9]+ samples)?[;,] through [a-z0-9_]+ with "
                 -P "${_vskd}")
# issue 123's ladder on the same frame under the plain set with the
# pre-pass off (the Turnip profile's program entries): _123_c3 the probe's
# sabotage (the draw's bytes right): wrong at 200, H1, the probe's column 3
# (the bones through the ring at the draw's block) right and every twin C
# equal to B; _123_nan_c the ring's NaN: twin C reads it as A does (differs
# from B); _123_bones_ring the ring builds in force ([dev] vu_bones = ring):
# clean, the start line, "the ring read: healed"; _123_bones_ring_nan with
# the NaN: the twins (both ring reads) differ, "not healed"
set(_vsk_ringenv "ICO_RD_VU_SET=plain;ICO_RD_VU_PREPASS=0")
add_test(NAME rd_vu_skin_check_123_c3
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> ${_vsk123}
                 "-DREQUIRE=skin_checks|skin_wrong|skin_ring_twins"
                 "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_123_c3"
                 "-DLINE=[0-9]+ skinned commands, [0-9]+ checks, [1-9][0-9]* wrong \\(address 200\\), 0 blocks whose bytes differed \\(0 changed after the draw\\).*reads as: H1, address-dependent.*the ring read: c3 right [1-9][0-9]* \\(c3 drawn in [1-9][0-9]*\\), ring twins [1-9][0-9]* read, 0 differ from B"
                 "-DLOG=\\(c3\\) the bones through the ring: right \\(0 wrong\\)" -P "${_vskd}")
add_test(NAME rd_vu_skin_check_123_nan_c
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> ${_vsk123}
                 "-DREQUIRE=skin_checks|skin_wrong|skin_dropped|skin_ring_twins"
                 "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_123_nan_c"
                 "-DLINE=[0-9]+ skinned commands, [0-9]+ checks, [1-9][0-9]* wrong \\(address 200\\).*reads as: H2, a port race.*ring twins [1-9][0-9]* read, [1-9][0-9]* differ from B"
                 "-DLOG=B \\(fresh, phase [0-9]+\\) [0-9]+ samples, C \\(ring read\\) [0-9]+ samples" -P "${_vskd}")
add_test(NAME rd_vu_skin_check_123_bones_ring
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> ${_vsk123}
                 -DREQUIRE=skin_checks "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_123_bones_ring"
                 "-DLINE=${_vskd_clean}.*twins: [1-9][0-9]* read \\([0-9]+ through the draw's own pipeline\\), 0 differ"
                 "-DLOG=rd: VU bones: through the ring \\(storage buffer\\) on the skinned entries.*rd: VU skin check: the ring read: healed \\(0 of [1-9][0-9]* twins differ"
                 -P "${_vskd}")
add_test(NAME rd_vu_skin_check_123_bones_ring_nan
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> ${_vsk123}
                 "-DREQUIRE=skin_checks|skin_wrong|skin_dropped"
                 "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_123_bones_ring_nan"
                 "-DLINE=[0-9]+ skinned commands, [0-9]+ checks, [1-9][0-9]* wrong \\(address 200\\).*twins: [1-9][0-9]* read \\([0-9]+ through the draw's own pipeline\\), [1-9][0-9]* differ"
                 "-DLOG=A and B read the bones through the ring: step 1 of the ladder in force.*rd: VU skin check: the ring read: not healed \\([1-9][0-9]* of [1-9][0-9]* twins differ"
                 -P "${_vskd}")
set_tests_properties(rd_vu_skin_check_123_c3 PROPERTIES ENVIRONMENT
                     "${_env};${_vsk_ringenv};ICO_RD_VU_SKIN_SABOTAGE=200")
set_tests_properties(rd_vu_skin_check_123_nan_c PROPERTIES ENVIRONMENT
                     "${_env};${_vsk_ringenv};ICO_RD_VU_SKIN_SABOTAGE=200,nan")
set_tests_properties(rd_vu_skin_check_123_bones_ring PROPERTIES ENVIRONMENT
                     "${_env};${_vsk_ringenv};ICO_RD_VU_BONES=ring")
set_tests_properties(rd_vu_skin_check_123_bones_ring_nan PROPERTIES ENVIRONMENT
                     "${_env};${_vsk_ringenv};ICO_RD_VU_BONES=ring;ICO_RD_VU_SKIN_SABOTAGE=200,nan")
set(_vskd_fx_tests rd_vu_skin_check_dump rd_vu_skin_check_163_probe rd_vu_skin_check_163_ring
                   rd_vu_skin_check_123 rd_vu_skin_check_123_nan rd_vu_skin_check_123_attach
                   rd_vu_skin_check_123_nan_attach rd_vu_skin_check_123_c3
                   rd_vu_skin_check_123_nan_c rd_vu_skin_check_123_bones_ring
                   rd_vu_skin_check_123_bones_ring_nan)
set_tests_properties(${_vskd_fx_tests} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 1800)
if(_env)
    set_tests_properties(rd_vu_skin_check_dump rd_vu_skin_check_123 PROPERTIES ENVIRONMENT
                                                                         "${_env}")
endif()
set_tests_properties(rd_vu_skin_check_163_probe
                     PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_SKIN_SABOTAGE=200")
set_tests_properties(rd_vu_skin_check_163_ring
                     PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_SKIN_SABOTAGE=200,ring")
set_tests_properties(rd_vu_skin_check_123_nan
                     PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_SKIN_SABOTAGE=200,nan")
set_tests_properties(rd_vu_skin_check_123_attach PROPERTIES ENVIRONMENT "${_env};${_vsk_attach}")
set_tests_properties(rd_vu_skin_check_123_nan_attach
                     PROPERTIES ENVIRONMENT "${_env};${_vsk_attach};ICO_RD_VU_SKIN_SABOTAGE=200,nan")

# rd_vu_skin_check_ab (test/rd_vu_skin_check_ab.cmake, review2 S6): the
# picture with the check on for every skinned command (ICO_RD_VU_SKIN_CHECK=2)
# against off, every pass-dump PNG byte for byte, at Original, with the
# attachment read and GS state as shader data (the Turnip reporter's
# profile on lavapipe) and at Enhanced 2x MSAA 4, on the fixtures of
# test/fixtures/lists/skin_check_ab.txt (the 2026-10-07 capture with the
# most skinned commands, issue 163's and issue 123's blended frames); the
# twins (every skinned draw twinned in its pass) drawn in the plain and
# attach runs and, in the attach runs, through the draws' own pipelines with
# the write mask cleared; the check's drawn counts as each fixture declares
# them; SKIP without a device
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_ab")
add_test(NAME rd_vu_skin_check_ab
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> ${_vu_fx}
                 "-DLIST=${ICO_FIXTURE_DIR}/lists/skin_check_ab.txt"
                 "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_skin_check_ab"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_skin_check_ab.cmake")
set_tests_properties(rd_vu_skin_check_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 1800)
if(_env)
    set_tests_properties(rd_vu_skin_check_ab PROPERTIES ENVIRONMENT "${_env}")
endif()

# rd_vu_bones_ring_ab (test/rd_vu_bones_ring_ab.cmake, issue 123's ladder,
# docs/port/VU_SKIN_CHECK.md "The ladder"): the fixtures of
# test/fixtures/lists/bones_ring.txt (issue 163's and issue 123's blended
# frames) replayed under the plain set with the VU pre-pass off, the bones
# through VuBoneCB and through the ring ([dev] vu_bones = ring): every
# pass-dump PNG byte for byte, at Original and with the attachment read and
# GS state as shader data; twin C drawn in the check runs as each fixture
# declares it; SKIP without a device
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vu_bones_ring_ab")
add_test(NAME rd_vu_bones_ring_ab
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> ${_vu_fx}
                 "-DLIST=${ICO_FIXTURE_DIR}/lists/bones_ring.txt"
                 "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_bones_ring_ab"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_bones_ring_ab.cmake")
set_tests_properties(rd_vu_bones_ring_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 1800)
if(_env)
    set_tests_properties(rd_vu_bones_ring_ab PROPERTIES ENVIRONMENT "${_env}")
endif()
# rd_vu_bones_ring_ab_prepass: the same fixtures with the VU pre-pass on
# (every tile-based GPU since v0.11.0): the ring runs byte for byte as the
# cbuffer runs under the pre-pass's batch builds (which read the bones
# through t5 either way) and with them off (the skinned commands then drawn
# the old way through the ring builds); the twins of the record draws read
# the bones through the ring as the record did
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vu_bones_ring_ab_prepass")
add_test(NAME rd_vu_bones_ring_ab_prepass
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> -DPREPASS=1 ${_vu_fx}
                 "-DLIST=${ICO_FIXTURE_DIR}/lists/bones_ring.txt"
                 "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_bones_ring_ab_prepass"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_bones_ring_ab.cmake")
set_tests_properties(rd_vu_bones_ring_ab_prepass PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                             TIMEOUT 2700)
if(_env)
    set_tests_properties(rd_vu_bones_ring_ab_prepass PROPERTIES ENVIRONMENT "${_env}")
endif()
