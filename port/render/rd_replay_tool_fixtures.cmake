# rd_replay_tool_fixtures.cmake: the replay tool's still re-present, held glow
# and saved blended frame, and the fog's depth read on a fogged frame, on the
# committed fixtures (docs/port/TESTING_FIXTURES.md; their dumps of the
# corpus, the godrays pairs, issue 163's pair and the dark hall are gone).
# Included from CMakeLists.txt inside its ICO_RUN_TESTS block, after the fog's
# self-tests: _env (the validation layer's environment) and _dc (the depth
# copy tests' folder) are that block's.  Every test reads its fixtures from a
# list under port/render/test/fixtures/lists/ and asserts the path ran.
include(rd_fixture.cmake)
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    set(_rtf_fx -DFIXTURE_TOOL=$<TARGET_FILE:rd_fixture_tool> "-DFIXTURES=${ICO_FIXTURE_DIR}"
                "-DFIXTURE_HELPERS=${ICO_FIXTURE_HELPERS}")
    # the still re-present (rd_reshow.c): a frame as its own tick before
    # (nothing moves), 4 presents at Enhanced 4:3 2x at 1280x960: presents
    # 1..3 shown again, all 4 pictures byte for byte the ones rendered with
    # ICO_RD_STILL_REPRESENT=0 (the lavapipe A/B)
    add_test(NAME rd_replay_tool_still
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> ${_rtf_fx}
                     "-DLIST=${ICO_FIXTURE_DIR}/lists/replay_still.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_still" -DPRESENTS=4
                     "-DARGS=--enhanced|--aspect|4:3|--resolution|2x|--present|1280x960"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_replay_tool_still.cmake")
    set_tests_properties(rd_replay_tool_still PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 600)
    # the glow held between ticks (rd_glow_hold.c) against its chain drawn at
    # every present (ICO_RD_GLOW_REDRAW=1), 2 presents a tick at Enhanced 4:3
    # 2x, every present rendered: a still tick (a frame as its own tick
    # before) the same bytes both ways, SCENE and the presented picture; a
    # moving one (two consecutive ticks) the first present the same bytes and
    # the later one differing only within 128 texels of the glow's mask
    # (WORK2) (the lavapipe A/B)
    set(_gh_py "")
    if(Python3_FOUND)
        set(_gh_py "-DPYTHON=${Python3_EXECUTABLE}")
    endif()
    add_test(NAME rd_replay_tool_glow_hold
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> ${_rtf_fx}
                     "-DLIST=${ICO_FIXTURE_DIR}/lists/glow_hold.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_glow_hold" -DPRESENTS=2
                     -DREACH=128 "-DARGS=--enhanced|--aspect|4:3|--resolution|2x" ${_gh_py}
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_replay_tool_glow_hold.cmake")
    set_tests_properties(rd_replay_tool_glow_hold PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                             TIMEOUT 900)
    # the blended present's frame F12 saves (rd.h rd_dump_blended_arm),
    # written by --save-blended from --interp T PREV, replayed on its own:
    # SCENE and the presented picture byte for byte the --interp run's, and
    # the same --interp run with the interpolation and the replays on the
    # render thread (--render-thread sync and on: the picture and the saved
    # frame byte for byte the main thread's).
    # The main gate's two ticks at alpha 0.5, Enhanced 4:3 2x; a tablet's two
    # ticks with the ticks' cameras at issue 163's present alpha 0.999000013,
    # 2x under Full pixel (the lavapipe A/B)
    add_test(NAME rd_replay_tool_save_blended
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> ${_rtf_fx}
                     "-DLIST=${ICO_FIXTURE_DIR}/lists/save_blended.txt" -DT=0.5
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_save_blended"
                     "-DARGS=--enhanced|--aspect|4:3|--resolution|2x" "-DRT=sync|on"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_replay_tool_save_blended.cmake")
    add_test(NAME rd_replay_tool_save_blended_163
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> ${_rtf_fx}
                     "-DLIST=${ICO_FIXTURE_DIR}/lists/save_blended_163.txt" -DT=0.999000013
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_save_blended_163"
                     "-DARGS=--resolution|2x|--full-height|--full-pixel" "-DRT=sync|on"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_replay_tool_save_blended.cmake")
    set_tests_properties(rd_replay_tool_save_blended rd_replay_tool_save_blended_163
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 900)
    # a fogged frame (the fog on SCENE's depth, the aura on it after) replayed
    # with the depth in place, with its copy and through a buffer: SCENE byte
    # for byte the same, on D32S8 and on D24S8; each run through the path it
    # asked for, and the copy's sabotaged reads change SCENE (the fog read
    # the depth)
    add_test(NAME rd_fog_depth_copy_dump
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> ${_rtf_fx}
                     "-DLIST=${ICO_FIXTURE_DIR}/lists/fog_depth_copy.txt" "-DOUT=${_dc}/dump"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_depth_copy_dump.cmake")
    add_test(NAME rd_fog_depth_copy_dump_d24s8
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> ${_rtf_fx}
                     "-DLIST=${ICO_FIXTURE_DIR}/lists/fog_depth_copy.txt"
                     "-DOUT=${_dc}/dump_d24s8" "-DEXTRA_ENV=ICO_VK_FAKE_D24S8=1" -DNO_BUFFER=1
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_depth_copy_dump.cmake")
    set_tests_properties(rd_fog_depth_copy_dump rd_fog_depth_copy_dump_d24s8
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 600)
    if(_env)
        set_tests_properties(rd_fog_depth_copy_dump rd_fog_depth_copy_dump_d24s8
                             PROPERTIES ENVIRONMENT "${_env}")
    endif()
endif()
