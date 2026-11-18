# rd_draw_pattern.cmake: the draw pattern gate (test/rd_draw_pattern.cmake;
# docs/port/TESTING.md, "The draw pattern gate"; issue 179), included from
# port/render/CMakeLists.txt before rd_asan_fixture_leaks.cmake.
#
# rd_draw_pattern_mali, _powervr, _adreno, _desktop: the fixtures of
# test/fixtures/lists/draw_pattern.txt (rd_fixture.cmake; an absent one FAILS)
# replayed once each under a faked device profile with rd_replay_tool --stats
# --game-selftests, the counts that say how each frame is drawn (GPU draws,
# passes, the VU draw merge, code 36's pairs fused and kept apart by reason, the
# two-pass blend's draws drawn twice, the pipelines made) equal to the profile's
# table test/draw_pattern/<profile>.txt; a table marked ungenerated fails.  No
# device: SKIP.
#
# rd_draw_pattern_tables (a target, never a test): the four tables written from
# this build's replays (-DWRITE=1), for a change of draw pattern that is
# intended: cmake --build <build dir> --target rd_draw_pattern_tables
include(rd_fixture.cmake)
if(TARGET rd_replay_tool AND TARGET rd_fixture_tool)
    set(_dp_profiles mali powervr adreno desktop)
    set(_dp_args -DTOOL=$<TARGET_FILE:rd_replay_tool>
                 -DFIXTURE_TOOL=$<TARGET_FILE:rd_fixture_tool> "-DFIXTURES=${ICO_FIXTURE_DIR}"
                 "-DFIXTURE_HELPERS=${ICO_FIXTURE_HELPERS}"
                 "-DLIST=${ICO_FIXTURE_DIR}/lists/draw_pattern.txt")
    set(_dp_write "")
    foreach(_dp IN LISTS _dp_profiles)
        list(APPEND _dp_write COMMAND "${CMAKE_COMMAND}" ${_dp_args} -DPROFILE=${_dp}
             "-DTABLE=${CMAKE_CURRENT_SOURCE_DIR}/test/draw_pattern/${_dp}.txt"
             "-DTABLE_SHOWN=port/render/test/draw_pattern/${_dp}.txt"
             "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/draw_pattern/${_dp}" -DWRITE=1
             -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_draw_pattern.cmake")
    endforeach()
    add_custom_target(rd_draw_pattern_tables ${_dp_write}
                      COMMENT "the draw pattern tables (port/render/test/draw_pattern) from this build's replays"
                      VERBATIM USES_TERMINAL)
    add_dependencies(rd_draw_pattern_tables rd_replay_tool rd_fixture_tool)
    if(ICO_RUN_TESTS)
        foreach(_dp IN LISTS _dp_profiles)
            file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/draw_pattern/${_dp}")
            add_test(NAME rd_draw_pattern_${_dp}
                     COMMAND "${CMAKE_COMMAND}" ${_dp_args} -DPROFILE=${_dp}
                             "-DTABLE=${CMAKE_CURRENT_SOURCE_DIR}/test/draw_pattern/${_dp}.txt"
                             "-DTABLE_SHOWN=port/render/test/draw_pattern/${_dp}.txt"
                             "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/draw_pattern/${_dp}"
                             -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_draw_pattern.cmake")
            set_tests_properties(rd_draw_pattern_${_dp} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                                   TIMEOUT 900)
            if(_env)
                set_tests_properties(rd_draw_pattern_${_dp} PROPERTIES ENVIRONMENT "${_env}")
            endif()
        endforeach()
    endif()
endif()
