# slow_judge.cmake: what slow pictures and steps are judged against, and the
# game's rate over its own frames (slow_judge.c, slow_judge.h; issue 179),
# included at the end of port/platform/CMakeLists.txt.
target_sources(ico_platform PRIVATE slow_judge.c)

# slow_judge_test: CPU only: a table of pacing states (the PAL disc at Frame
# rate Original on 60 Hz, 60 a second on 120 Hz, Uncapped, a cap above the
# refresh, nothing known) with the slow-picture and slow-step bounds and the
# rate in force; the reporter's 18.7 ms pictures and 11 ms steps not slow at
# 25 a second; the step's CPU words; the game's rate from its frame closes
# (25.0, not 25.1) and from the wall time; the line word for word
add_executable(slow_judge_test test/slow_judge_test.c)
target_include_directories(slow_judge_test PRIVATE "${ICO_COMPAT_DIR}")
target_compile_options(slow_judge_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(slow_judge_test PRIVATE ico_platform)
if(ICO_RUN_TESTS)
    add_test(NAME slow_judge COMMAND slow_judge_test)
endif()
