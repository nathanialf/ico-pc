# rd_early.cmake: the first 3D pictures' allocations made at start-up
# (rd_vu_rec_early.h, rd_early_plan.h; docs/port/RENDER.md "Made at
# start-up"), included at the end of port/render/CMakeLists.txt (a file of
# its own: port/render/CMakeLists.txt is at the 256 KiB cap,
# tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_early_plan.c rd_vu_rec_early.c)

# rd_early_plan_test: the plan's bound with the memory numbers of the two
# v0.11.3 loss reports (issues 179 and 211), a desktop without a measure,
# Direct3D's free video memory, under the floor and after the system's
# signal (CPU only)
add_executable(rd_early_plan_test test/rd_early_plan_test.c rd_early_plan.c)
target_include_directories(rd_early_plan_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(rd_early_plan_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME rd_early_plan COMMAND rd_early_plan_test)

    # rd_vu_rec_early_ab, _compute, _targets (test/rd_vu_env_ab.cmake): the
    # dumps of tools/vustage_dumps.txt with the record grown by the frame as
    # in play (ICO_RD_EARLY_PREPARE=0) and made at start at its largest, 1024
    # rows, with the mesh arena's first chunk (=1: the replay tool calls
    # rd_vu_record_prepare as the game window does, without a memory
    # function): every named target byte for byte at Original 4:3 1x and
    # Enhanced 16:9 2x; the record's three writes (lavapipe's own, the
    # compute dispatches of issue 179's Mali, the render targets).  A frame
    # without a pre-pass is skipped; no dump or no device: SKIP
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vu_rec_early_ab"
         "${CMAKE_CURRENT_BINARY_DIR}/vu_rec_early_ab_compute"
         "${CMAKE_CURRENT_BINARY_DIR}/vu_rec_early_ab_targets")
    set(_early_req_a "stats: VU pre-pass on: [1-9][0-9]* passes")
    set(_early_req_b "early: record 1024 rows, arena made.*stats: VU pre-pass on: [1-9][0-9]* passes")
    foreach(_early IN ITEMS "" compute targets)
        if(_early STREQUAL "")
            set(_early_name rd_vu_rec_early_ab)
            set(_early_dir vu_rec_early_ab)
            set(_early_a "ICO_RD_EARLY_PREPARE=0")
            set(_early_b "ICO_RD_EARLY_PREPARE=1")
        else()
            set(_early_name rd_vu_rec_early_ab_${_early})
            set(_early_dir vu_rec_early_ab_${_early})
            set(_early_a "ICO_RD_VU_RECORD=${_early}|ICO_RD_EARLY_PREPARE=0")
            set(_early_b "ICO_RD_VU_RECORD=${_early}|ICO_RD_EARLY_PREPARE=1")
        endif()
        add_test(NAME ${_early_name}
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         -DNAME=${_early_name}
                         "-DLIST=${CMAKE_SOURCE_DIR}/tools/vustage_dumps.txt"
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/${_early_dir}"
                         "-DSKIP_UNLESS=stats: VU pre-pass on: [1-9][0-9]* passes"
                         "-DENV_A=${_early_a}" "-DENV_B=${_early_b}"
                         "-DREQ_A=${_early_req_a}" "-DREQ_B=${_early_req_b}"
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_env_ab.cmake")
        set_tests_properties(${_early_name} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                       TIMEOUT 3600)
        if(_env)
            set_tests_properties(${_early_name} PROPERTIES ENVIRONMENT "${_env}")
        endif()
    endforeach()
endif()
