# rd_smallcuts.cmake: v0.11.6's small cuts for the PowerVR phone's opening,
# included at the end of port/render/CMakeLists.txt (a file of its own:
# port/render/CMakeLists.txt is near the 256 KiB cap, tools/check_no_rom.sh).
#   the turn in the present's blit (rd_turn_fold.c, rd_turn_fold.h;
#     RD_TURN_FOLD_DEFAULT 0, ICO_RD_TURN_FOLD)
#   the VU record by compute on the PowerVR class (rd_vu_class.c,
#     rd_vu_class.h; RD_VU_COMPUTE_CLASS_DEFAULT 0, ICO_RD_VU_COMPUTE_CLASS)
# docs/port/RENDER_POST.md has a section for each.
target_sources(ico_render PRIVATE rd_turn_fold.c rd_vu_class.c)

# rd_smallcuts_test: the pure parts (CPU): the turned rect against the
# turn's pixel table (every quarter turn, the phone's window and odd sizes),
# the reasons a present keeps the turn pass, the PowerVR class's rule through
# the attempt ledger's decider
add_executable(rd_smallcuts_test test/rd_smallcuts_test.c)
target_compile_options(rd_smallcuts_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_smallcuts_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                     "${CMAKE_SOURCE_DIR}/port/rhi"
                                                     "${CMAKE_SOURCE_DIR}/port/shaders"
                                                     "${CMAKE_SOURCE_DIR}/port/platform")
target_link_libraries(rd_smallcuts_test PRIVATE ico_render)
if(ICO_RUN_TESTS)
    add_test(NAME rd_smallcuts COMMAND rd_smallcuts_test)
endif()

# rd_turn_fold_ab (test/rd_turn_fold_ab.cmake): rd_pixel's dump and every dump
# of the machine-local tools/turn_fold_dumps.txt (the PowerVR phone's three
# opening dumps, issue 179's three Mali-G52 dumps; a listed dump that is absent
# fails where /primary/dev/ico-dumps exists) through the headless twin at 90,
# 180 and 270 degrees: the present drawn turned by its box blit
# (ICO_RD_TURN_FOLD=1) against the turn pass (unset), every PNG byte for byte;
# the fold's self-test passed; the phone's setting and a 16:9 desktop window
# at its own resolution fold (or keep the turn pass for drawn text), 1x at
# 640x480 keeps it (scaled), the overlay test pattern keeps it.  No device:
# SKIP.
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/turn_fold_ab")
    add_test(NAME rd_turn_fold_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_frame.rddump"
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/turn_fold_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/turn_fold_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_turn_fold_ab.cmake")
    set_tests_properties(rd_turn_fold_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                    FIXTURES_REQUIRED rd_dump TIMEOUT 7200)
    if(_env)
        set_tests_properties(rd_turn_fold_ab PROPERTIES ENVIRONMENT "${_env}")
    endif()
endif()

# The VU record by compute on the PowerVR class, on lavapipe by the tests'
# switch (lavapipe offers both stores; ICO_RD_VU_COMPUTE_CLASS=1 takes the
# rule on any class):
#   rd_vuprepass_selftest_compute_class  the start-up self-test passes with
#                      the record by compute, the rule's line names the switch
#   rd_vuprepass_selftest_compute_class_back  a stale compute record
#                      (ICO_RD_VU_PREPASS_SABOTAGE=stale) goes back to the
#                      vertex stores first (the rule's refusal line), never
#                      straight to the render targets
#   rd_vuprepass_selftest_compute_class_off  the switch at 0 on lavapipe: the
#                      stores, no rule line for the compute
#   rd_vurecord_ab_compute_class  rd_vu_env_ab: the switch at 0 (the stores)
#                      against 1 (the compute dispatches by the rule), every
#                      named target byte for byte, B's stats naming the rule
#   rd_vurecord_ab_compute_class_opening  the same over tools/lamps_dumps.txt
#                      (the phone's opening dumps among them; LIST_STRICT)
if(ICO_RUN_TESTS AND TARGET rd_vuprepass_selftest_test AND DEFINED _vst)
    set(_vcc_line "rd: VU record: the PowerVR class's rule: the compute dispatches in place of the vertex stores \\(ICO_RD_VU_COMPUTE_CLASS=1: the tests' switch on another class, RD_VU_COMPUTE_CLASS_DEFAULT [01]\\)")
    set(_vcc_rec "rd: VU pre-pass: on \\(self-test: [^\n]* ms, record compute")
    add_test(NAME rd_vuprepass_selftest_compute_class
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}" "-DEXTRA=${_vcc_line}.*${_vcc_rec}" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_compute_class_back
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=off
                     "-DLINE=${_vst_stale}"
                     "-DEXTRA=${_vcc_line}.*rd: VU record: stored by the vertex shaders into the frame slot's record buffer: no render targets, no copies \\(the PowerVR class's compute record off: they failed the start-up self-test\\)"
                     -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_compute_class_off
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: VU record: the PowerVR class's rule: the record as auto takes it \\(ICO_RD_VU_COMPUTE_CLASS=0: the tests' switch"
                     "-DNOT_=record compute" -P "${_vst}")
    set(_vcc_tests rd_vuprepass_selftest_compute_class rd_vuprepass_selftest_compute_class_back
                   rd_vuprepass_selftest_compute_class_off)
    set_tests_properties(${_vcc_tests} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
    set_tests_properties(rd_vuprepass_selftest_compute_class
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_COMPUTE_CLASS=1")
    set_tests_properties(
        rd_vuprepass_selftest_compute_class_back
        PROPERTIES ENVIRONMENT
                   "${_env};ICO_RD_VU_COMPUTE_CLASS=1;ICO_RD_VU_PREPASS_SABOTAGE=stale")
    set_tests_properties(rd_vuprepass_selftest_compute_class_off
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_COMPUTE_CLASS=0")
endif()
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    foreach(_vcc ab opening)
        if(_vcc STREQUAL "ab")
            set(_vcc_name rd_vurecord_ab_compute_class)
            set(_vcc_list "${CMAKE_SOURCE_DIR}/tools/vustage_dumps.txt")
            set(_vcc_strict "")
        else()
            set(_vcc_name rd_vurecord_ab_compute_class_opening)
            set(_vcc_list "${CMAKE_SOURCE_DIR}/tools/lamps_dumps.txt")
            set(_vcc_strict -DLIST_STRICT=1)
        endif()
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${_vcc_name}")
        add_test(NAME ${_vcc_name}
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         -DNAME=${_vcc_name}
                         "-DDUMP=${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_frame.rddump"
                         "-DLIST=${_vcc_list}"
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/${_vcc_name}"
                         "-DENV_A=ICO_RD_VU_COMPUTE_CLASS=0" "-DENV_B=ICO_RD_VU_COMPUTE_CLASS=1"
                         "-DREQ_A=stats: VU record stores: stored by the vertex shaders"
                         "-DREQ_B=copies, [0-9]+ dispatches.*stats: VU record compute: stored by compute dispatches into the frame slot's record buffer: no render targets, no copies \\(the PowerVR class's rule"
                         ${_vcc_strict}
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_env_ab.cmake")
        set_tests_properties(${_vcc_name} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                     TIMEOUT 3600 FIXTURES_REQUIRED rd_dump)
        if(_env)
            set_tests_properties(${_vcc_name} PROPERTIES ENVIRONMENT "${_env}")
        endif()
    endforeach()
endif()
