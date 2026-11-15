# rd_early.cmake: the first 3D pictures' allocations made at start-up
# (rd_vu_rec_early.h, rd_early_plan.h; docs/port/RENDER.md "Made at
# start-up"), included at the end of port/render/CMakeLists.txt (a file of
# its own: port/render/CMakeLists.txt is at the 256 KiB cap,
# tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_early_plan.c rd_vu_rec_early.c)
# one log line as one write (rd_log_line.h: rd__log, rd__log_once) and the
# start-up items of v0.11.5 (docs/port/RENDER_POST.md "Start-up")
target_sources(ico_render PRIVATE rd_log_line.c rd_made_outside.c rd_made_outside_text.c)

# rd_early_plan_test: the plan's bound with the memory numbers of the two
# v0.11.3 loss reports (issues 179 and 211), a desktop without a measure,
# Direct3D's free video memory, under the floor and after the system's
# signal (CPU only)
add_executable(rd_early_plan_test test/rd_early_plan_test.c rd_early_plan.c)
target_include_directories(rd_early_plan_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(rd_early_plan_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
# rd_log_line_test: a line handed to an unbuffered stream in one write,
# short, with a suffix, past the stack's buffer, at its edge (CPU only)
add_executable(rd_log_line_test test/rd_log_line_test.c rd_log_line.c)
target_include_directories(rd_log_line_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(rd_log_line_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
# rd_made_outside_test: the made-in-play line (rd_made_outside.h: the
# pipelines and buffers made outside the key cache, named) and its limiter
# (CPU only)
add_executable(rd_made_outside_test test/rd_made_outside_test.c rd_made_outside_text.c)
target_include_directories(rd_made_outside_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(rd_made_outside_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME rd_early_plan COMMAND rd_early_plan_test)
    add_test(NAME rd_log_line COMMAND rd_log_line_test)
    add_test(NAME rd_made_outside COMMAND rd_made_outside_test)

    # rd_vu_rec_early_ab, _compute, _targets (test/rd_vu_env_ab.cmake): the
    # fixtures of test/fixtures/lists/vustage.txt with the record grown by the frame as
    # in play (ICO_RD_EARLY_PREPARE=0) and made at start at its largest, 1024
    # rows (ICO_RD_EARLY_ROWS=1024: the run's need would be 512 there), with
    # the mesh arena's first chunk (=1: the replay tool calls
    # rd_vu_record_prepare as the game window does, without a memory
    # function): every named target byte for byte at Original 4:3 1x and
    # Enhanced 16:9 2x; the record's three writes (lavapipe's own, the
    # compute dispatches of issue 179's Mali, the render targets), each
    # fixture's record vertices on both runs (and its compute dispatches
    # under the compute record); no device: SKIP
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vu_rec_early_ab"
         "${CMAKE_CURRENT_BINARY_DIR}/vu_rec_early_ab_compute"
         "${CMAKE_CURRENT_BINARY_DIR}/vu_rec_early_ab_targets")
    include(rd_fixture.cmake)
    set(_early_fx -DFIXTURE_TOOL=$<TARGET_FILE:rd_fixture_tool> "-DFIXTURES=${ICO_FIXTURE_DIR}"
                  "-DFIXTURE_HELPERS=${ICO_FIXTURE_HELPERS}")
    set(_early_req_a "stats: VU pre-pass on: [1-9][0-9]* passes")
    set(_early_req_b "early: record 1024 rows, arena made.*stats: VU pre-pass on: [1-9][0-9]* passes")
    foreach(_early IN ITEMS "" compute targets)
        if(_early STREQUAL "")
            set(_early_name rd_vu_rec_early_ab)
            set(_early_dir vu_rec_early_ab)
            set(_early_a "ICO_RD_EARLY_PREPARE=0")
            set(_early_b "ICO_RD_EARLY_PREPARE=1|ICO_RD_EARLY_ROWS=1024")
            set(_early_q "prepass_vertices")
        else()
            set(_early_name rd_vu_rec_early_ab_${_early})
            set(_early_dir vu_rec_early_ab_${_early})
            set(_early_a "ICO_RD_VU_RECORD=${_early}|ICO_RD_EARLY_PREPARE=0")
            set(_early_b "ICO_RD_VU_RECORD=${_early}|ICO_RD_EARLY_PREPARE=1|ICO_RD_EARLY_ROWS=1024")
            set(_early_q "prepass_vertices")
            if(_early STREQUAL "compute")
                set(_early_q "prepass_vertices|vu_record_dispatches")
            endif()
        endif()
        add_test(NAME ${_early_name}
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         -DNAME=${_early_name} ${_early_fx}
                         "-DLIST=${ICO_FIXTURE_DIR}/lists/vustage.txt" "-DFX_A=${_early_q}"
                         "-DFX_B=${_early_q}"
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

    # rd_vu_rec_size_ab, _mali (test/rd_vu_env_ab.cmake): the record made at
    # start-up at the run's need (rd_early_plan.h rd_early_start_rows: 512
    # rows, the tool's presents not interpolated, no velocity) against
    # v0.11.5's 1024 rows (ICO_RD_EARLY_ROWS=1024), over the fixtures of
    # test/fixtures/lists/vu_rec_size.txt (issue 179's Mali frames, the
    # Pixel's three opening frames, the corpus's largest pre-pass record), each
    # fixture's record vertices on both runs (and its compute dispatches on
    # _mali): every named
    # target byte for byte at Original 4:3 1x and Enhanced 16:9 2x, and no
    # growth in play past the 512 rows (the need's margin holds on every
    # listed dump); _mali under the Arm fakes and the compute record (the
    # reporter's write); no device: SKIP
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vu_rec_size_ab"
         "${CMAKE_CURRENT_BINARY_DIR}/vu_rec_size_ab_mali")
    foreach(_size IN ITEMS "" mali)
        if(_size STREQUAL "")
            set(_size_name rd_vu_rec_size_ab)
            set(_size_dir vu_rec_size_ab)
            set(_size_env "")
            set(_size_q "prepass_vertices")
        else()
            set(_size_name rd_vu_rec_size_ab_mali)
            set(_size_dir vu_rec_size_ab_mali)
            set(_size_env "ICO_VK_FAKE_LIMITS=mali|ICO_VK_FAKE_VENDOR=0x13B5|ICO_RD_VU_RECORD=compute|")
            set(_size_q "prepass_vertices|vu_record_dispatches")
        endif()
        add_test(NAME ${_size_name}
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         -DNAME=${_size_name} ${_early_fx}
                         "-DLIST=${ICO_FIXTURE_DIR}/lists/vu_rec_size.txt" "-DFX_A=${_size_q}"
                         "-DFX_B=${_size_q}"
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/${_size_dir}"
                         "-DSKIP_UNLESS=stats: VU pre-pass on: [1-9][0-9]* passes"
                         "-DENV_A=${_size_env}ICO_RD_EARLY_PREPARE=1|ICO_RD_EARLY_ROWS=1024"
                         "-DENV_B=${_size_env}ICO_RD_EARLY_PREPARE=1"
                         "-DREQ_A=early: record 1024 rows.*stats: VU pre-pass on: [1-9][0-9]* passes"
                         "-DREQ_B=early: record 512 rows.*stats: VU pre-pass on: [1-9][0-9]* passes"
                         "-DFORBID_B=grown in play past"
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_env_ab.cmake")
        set_tests_properties(${_size_name} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                      TIMEOUT 3600)
        if(_env)
            set_tests_properties(${_size_name} PROPERTIES ENVIRONMENT "${_env}")
        endif()
    endforeach()
endif()
