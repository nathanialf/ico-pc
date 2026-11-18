# Included from port/render/CMakeLists.txt inside the skin check's tests
# (_vsk, _env in scope): the trace's device test (rd_vu_skin_trace.c,
# docs/port/VU_SKIN_CHECK.md "The trace"; issue 123). Its own file because
# port/render/CMakeLists.txt is at the 256 KiB cap (tools/check_no_rom.sh).
# stale: first the careful rule
# (the window's after a start-up set stopped for memory on Qualcomm's own
# driver) still compiles the box check's pipeline and draws the box
# check; then the stamps: four replays of the one-command frame under the
# game's mode, a probe each replay, each slot's readback not written
# again (rd__vu_skin_sabotage_copy): each says its readback was not
# written by its replay, naming the replay whose stamps it holds
add_test(NAME rd_vu_skin_check_stale
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vu_skin_check_test> -DMODE=stale
                 "-DEXTRA=rd: VU skin check: frame [0-9]+ command 0:[0-9]+ replay [0-9]+ \\(frame slot [0-9], readback of 1 checks\\): the readback was not written by this replay's copies \\(the probe target's clear stamp: replay [0-9]+'s \\(bits 0..23\\)[;,] the ring's stamp: replay [0-9]+'s\\)"
                 -P "${_vsk}")
set_tests_properties(rd_vu_skin_check_stale PROPERTIES ENVIRONMENT
                     "${_env};ICO_RD_VU_SKIN_INTERVAL_MS=0" SKIP_REGULAR_EXPRESSION "SKIP:")
# stale_phone, clean_phone: the same on a tile-based phone GPU (Arm faked,
# ICO_VK_FAKE_VENDOR=0x13B5: RhiLimits.tilePhone): none of the check's own
# pipelines compiled (issue 217: with the careful rule's reason, the line
# naming it; without one, in the frame check's mode 2 too, the line once at
# the first compile held back), no box check drawn, no stamps (no readback
# named not written); the clean frame checked right with the readback laid
# out without the ring stamp
add_test(NAME rd_vu_skin_check_stale_phone
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vu_skin_check_test> -DMODE=stale
                 "-DEXTRA=rd: VU skin check: its box check, its twins' pipelines and its c2 variant are not compiled in the background \\(the test's careful rule, a tile-based phone GPU: none, whatever the start-up set did, issue 217\\): the check runs without them and without column 3"
                 "-DNOT_=not written by this replay's copies|compiled carefully|not compiled in the background.*not compiled in the background"
                 -P "${_vsk}")
add_test(NAME rd_vu_skin_check_clean_phone
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vu_skin_check_test> -DMODE=clean
                 "-DEXTRA=rd: VU skin check: its box check, its twins' pipelines and its c2 variant are not compiled in the background \\(a tile-based phone GPU: none, whatever the start-up set did, issue 217\\): the check runs without them and without column 3"
                 "-DNOT_=vertices wrong|not written by this replay's copies|ring stamps of an earlier replay [1-9]|its box check made over every batch|not compiled in the background.*not compiled in the background"
                 -P "${_vsk}")
set_tests_properties(rd_vu_skin_check_stale_phone PROPERTIES
                     ENVIRONMENT "${_env};ICO_RD_VU_SKIN_INTERVAL_MS=0;ICO_VK_FAKE_VENDOR=0x13B5"
                     SKIP_REGULAR_EXPRESSION "SKIP:")
set_tests_properties(rd_vu_skin_check_clean_phone PROPERTIES
                     ENVIRONMENT "${_env};ICO_VK_FAKE_VENDOR=0x13B5" SKIP_REGULAR_EXPRESSION "SKIP:")
# thin_phone: the twins on a tile-based phone GPU (Arm faked) under the
# window's rule without a reason (rd_vu_skin_background(memory, NULL)): the
# line that none of the check's own pipelines is compiled (issue 217), once,
# at the window's call; thinned from the start (one skinned draw a picture,
# not every skinned draw of the first 3D scene), each twin picked not drawn
# (its form the box check's pipeline, never made there), so no twin differs
# and nothing re-arms them
add_test(NAME rd_vu_skin_check_thin_phone
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vu_skin_check_test> -DMODE=thinphone
                 "-DEXTRA=rd: VU skin check: its box check, its twins' pipelines and its c2 variant are not compiled in the background \\(a tile-based phone GPU: none, whatever the start-up set did, issue 217\\): the check runs without them and without column 3.*rd: VU skin check: twins on \\(asked \\(the tests' or the tool's\\)\\) at one skinned draw a picture from the start \\(a tile-based phone GPU: not every skinned draw of the first 3D scene, only through the draw's own pipeline where it reads its attachment, the box check's not compiled there\\)"
                 "-DNOT_=twins differ: |twins re-armed|not compiled in the background.*not compiled in the background"
                 -P "${_vsk}")
set_tests_properties(rd_vu_skin_check_thin_phone PROPERTIES
                     ENVIRONMENT "${_env};ICO_RD_VU_SKIN_INTERVAL_MS=off;ICO_VK_FAKE_VENDOR=0x13B5"
                     SKIP_REGULAR_EXPRESSION "SKIP:")
# reset (issue 123, v0.11.7; rd_vu_skin_check.h rd__skin_skip): the probe's
# bone at 200 read as zeros (wrong, its bytes right) with the GPU's own sign
# of a reset faked at the check's own replay: skipped after a reset, not
# wrong, no capture asked; the sign forgotten, the same check wrong at 200
# and the capture asked (no folder: no sidecar), as before
add_test(NAME rd_vu_skin_check_reset
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vu_skin_check_test> -DMODE=reset
                 "-DEXTRA=rd: VU skin check: frame [0-9]+ command 0:[0-9]+ mesh rd vu skin check test #[0-9]+ \\(recorded in replay [0-9]+\\): skipped after a reset: the GPU's own sign of a reset at replay [0-9]+, (within [0-9]+ replays of it|since the slot's check before \\(replay [0-9]+\\))[;,] it read [1-9][0-9]* of 120 vertices wrong.*${_vsk_wrong}.*rd: VU skin check: no sidecar written \\(no folder for it\\)[;,] asked for a frame dump"
                 -P "${_vsk}")
set_tests_properties(rd_vu_skin_check_reset PROPERTIES
                     ENVIRONMENT "${_env};ICO_RD_VU_SKIN_SABOTAGE=200" SKIP_REGULAR_EXPRESSION "SKIP:")
