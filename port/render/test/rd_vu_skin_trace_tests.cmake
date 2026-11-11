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
# ICO_VK_FAKE_VENDOR=0x13B5: RhiLimits.tilePhone), where the check issues what
# v0.11.2's issued: with the careful rule's reason none of its own compiles
# (v0.11.2's line), no stamps (no readback named not written); the clean frame
# checked right with the readback laid out without the ring stamp
add_test(NAME rd_vu_skin_check_stale_phone
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vu_skin_check_test> -DMODE=stale
                 "-DEXTRA=rd: VU skin check: its box check, its twins' pipelines and its c2 variant are not compiled in the background \\(the test's careful rule, a tile-based phone GPU: none"
                 "-DNOT_=not written by this replay's copies|compiled carefully"
                 -P "${_vsk}")
add_test(NAME rd_vu_skin_check_clean_phone
         COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vu_skin_check_test> -DMODE=clean
                 "-DNOT_=vertices wrong|not written by this replay's copies|ring stamps of an earlier replay [1-9]"
                 -P "${_vsk}")
set_tests_properties(rd_vu_skin_check_stale_phone PROPERTIES
                     ENVIRONMENT "${_env};ICO_RD_VU_SKIN_INTERVAL_MS=0;ICO_VK_FAKE_VENDOR=0x13B5"
                     SKIP_REGULAR_EXPRESSION "SKIP:")
set_tests_properties(rd_vu_skin_check_clean_phone PROPERTIES
                     ENVIRONMENT "${_env};ICO_VK_FAKE_VENDOR=0x13B5" SKIP_REGULAR_EXPRESSION "SKIP:")
