# rd_ratecard.cmake: the rate card (rd_ratecard.c the pure part,
# rd_ratecard_run.c the device's; rd_ratecard.h; docs/port/RENDER_PERF.md,
# "The rate card"; issue 179 diagnostics), included at the end of
# port/render/CMakeLists.txt (a file of its own: port/render/CMakeLists.txt
# is near the 256 KiB cap, tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_ratecard.c rd_ratecard_run.c)

# rd_ratecard_rule (test/rd_ratecard_test.c, CPU): the medians and spreads
# of synthetic timings, the unit arithmetic (a difference over a count, the
# spread against the subtracted frame's median, too few times, nothing to
# divide by, a difference not above 0, the ratio), the printed block word for
# word with a measured item, a ratio and "not measured", the block read back
# (rd_rc_parse), and the result file's decision: no file, another key,
# done (reprinted), measuring (skipped), blocked, a text it does not know
add_executable(rd_ratecard_test test/rd_ratecard_test.c)
target_compile_options(rd_ratecard_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_ratecard_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(rd_ratecard_test PRIVATE ico_render)
if(COMMAND ico_copy_sdl3_dll)
    ico_copy_sdl3_dll(rd_ratecard_test)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_ratecard_rule COMMAND rd_ratecard_test)
endif()

# rd_ratecard_mali, _lavapipe, _end (test/rd_ratecard.cmake): the card on
# lavapipe through rd_vuprepass_selftest_test (on, plain): under the Arm
# fakes without dual-source blending (the twins on, both twin items
# measured) and as lavapipe is (dual-source blending: the twins not
# applicable), each the block's lines (one an item of rd_ratecard.h's enum,
# read by the script) and the results file "done", then the reprint; _end
# the start ending inside it, then skipped twice ("blocked")
if(ICO_RUN_TESTS AND TARGET rd_vuprepass_selftest_test)
    set(_rc "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_ratecard.cmake")
    foreach(_rc_t mali lavapipe end)
        set(_rc_mode measure)
        set(_rc_twins 0)
        if(_rc_t STREQUAL "end")
            set(_rc_mode end)
        elseif(_rc_t STREQUAL "mali")
            set(_rc_twins 1)
        endif()
        add_test(NAME rd_ratecard_${_rc_t}
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test>
                         "-DFILE=${CMAKE_CURRENT_BINARY_DIR}/rd_ratecard_${_rc_t}.txt"
                         -DMODE=${_rc_mode} -DTWINS=${_rc_twins}
                         "-DHEADER=${CMAKE_CURRENT_SOURCE_DIR}/rd_ratecard.h" -P "${_rc}")
        set_tests_properties(rd_ratecard_${_rc_t} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
    endforeach()
    set_tests_properties(rd_ratecard_mali rd_ratecard_end
                         PROPERTIES ENVIRONMENT "${_env_mali_pre};ICO_VK_FAKE_NO_DUAL=1")
    if(_env)
        set_tests_properties(rd_ratecard_lavapipe PROPERTIES ENVIRONMENT "${_env}")
    endif()
endif()
