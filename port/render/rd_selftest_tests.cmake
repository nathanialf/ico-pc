# rd_selftest_tests.cmake: port/render/CMakeLists.txt's registrations of the
# blend policy's, the fog's, DATE's and the shadow count's self-test tests and
# the desktop's local reads' probe, moved verbatim (CMakeLists.txt is at the
# 256 KiB cap, tools/check_no_rom.sh); included where they stood, in the same
# scope.
# The blend policy (rd_dst_read.c)
# rd_dst_usage_test: the fixed-function blend by default makes no
# destination-read layout, shader twin, target usage or start-up key; an
# ICO_RD_BLEND=attachment opt-in makes the attachment read's alone.
add_executable(rd_dst_usage_test test/rd_dst_usage_test.c)
target_compile_options(rd_dst_usage_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_dst_usage_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_dst_usage_test PRIVATE ico_render)
if(ICO_RUN_TESTS)
    add_test(NAME rd_dst_usage COMMAND rd_dst_usage_test)
    set_tests_properties(rd_dst_usage PROPERTIES SKIP_RETURN_CODE 77)
    if(_env)
        set_tests_properties(rd_dst_usage PROPERTIES ENVIRONMENT "${_env}")
    endif()
    # the exact blend's attachment read, now an opt-in, by the tests that
    # drew it by default before (lavapipe has the read): the same
    # expectations and recorded hashes (rd_present's and rd_crt's *_EXACT),
    # rd_pixel's comparison of the modes, rd_perf's destination-read counts
    set(_env_blend_attachment ${_env} "ICO_RD_BLEND=attachment")
    set(_bat "${CMAKE_CURRENT_BINARY_DIR}/blend_attachment")
    file(MAKE_DIRECTORY "${_bat}")
    add_test(NAME rd_pixel_attachment COMMAND rd_pixel_test "${_bat}")
    add_test(NAME rd_fog_attachment COMMAND rd_fog_test)
    add_test(NAME rd_present_attachment COMMAND rd_present_test "${_bat}")
    add_test(NAME rd_crt_attachment COMMAND rd_crt_test "${_bat}")
    add_test(NAME rd_perf_attachment COMMAND rd_perf_test)
    set_tests_properties(rd_pixel_attachment rd_fog_attachment rd_present_attachment
                         rd_crt_attachment rd_perf_attachment
                         PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_bat}"
                                    ENVIRONMENT "${_env_blend_attachment}")
    # GS state as shader data (rd_uber.c) on lavapipe, which orders the
    # attachment read (ICO_RD_UBER=on: auto takes it on tile-based GPUs
    # only): rd_pixel's cases and expectations as under the attachment read
    # (its exact-mode matrix leaves out and counts the cells the colour read
    # draws its own way: the premultiplied subtracts' floor and DATE per
    # primitive; testUberSubtractOracle holds the subtracts to the GS's
    # arithmetic, testDateStencil checks the per-primitive DATE)
    set(_env_uber ${_env} "ICO_RD_UBER=on")
    set(_bub "${CMAKE_CURRENT_BINARY_DIR}/uber")
    file(MAKE_DIRECTORY "${_bub}")
    add_test(NAME rd_pixel_uber COMMAND rd_pixel_test "${_bub}")
    set_tests_properties(rd_pixel_uber PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_bub}"
                                                  ENVIRONMENT "${_env_uber}")
endif()

# The tiler passes (rd_replay.c: the fog's local read, DATE through
# the stencil bit, the blur pair fold)
# The fog's depth read inside the scene pass (ICO_RD_FOG_PATH=local: the
# profile's local read on lavapipe, which has it): rd_fog_test's grid, edge
# and above-1x cases on the local path, and case d's replays on the other
# paths the same pixels byte for byte; the fog's self-test tries local first
# and takes it (a scaled, wide Full pixel SCENE), its probe logged; DATE's
# self-test (ICO_RD_DATE_PATH=stencil) finds the stencil bit and the
# snapshot right on every sample.  rd_pixel's DATE case and rd_blur's fold
# case (b) run in their own tests.
if(ICO_RUN_TESTS)
    set(_env_fog_local ${_env} "ICO_RD_FOG_PATH=local")
    set(_fl "${CMAKE_CURRENT_BINARY_DIR}/fog_local")
    file(MAKE_DIRECTORY "${_fl}")
    add_test(NAME rd_fog_local COMMAND rd_fog_test)
    set_tests_properties(rd_fog_local PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${_fl}"
                                                 ENVIRONMENT "${_env_fog_local}")
    add_test(NAME rd_fog_selftest_local
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                     -DFOG_PATH=local
                     "-DRESULTS=local ok [0-2], copy not tried, inplace not tried, buffer not tried"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_fog_selftest.cmake")
    set_tests_properties(rd_fog_selftest_local PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                          ENVIRONMENT "${_env_fog_local}")
    set(_env_date_stencil ${_env} "ICO_RD_DATE_PATH=stencil")
    add_test(NAME rd_date_selftest
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test> -DPATH=stencil
                     "-DRESULTS=stencil ok 5, snapshot ok 5"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_date_selftest.cmake")
    set_tests_properties(rd_date_selftest PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                     ENVIRONMENT "${_env_date_stencil}")
    # DATE under GS state as shader data (rd_uber.c, ICO_RD_UBER=on): the
    # colour read's per-primitive DATE first (its fifth sample the GS's:
    # the second sprite fails over the first), then the stencil bit and the
    # snapshot with it held off (their fifth sample the alpha before the
    # draw); the path stays the colour read
    add_test(NAME rd_date_selftest_uber
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                     "-DPATH=colour read"
                     "-DRESULTS=colour read ok 5, stencil ok 5, snapshot ok 5"
                     "-DEXTRA=pipelines: GS state as shader data: on \\(raster-order read, \\[dev\\] uber = on\\)"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_date_selftest.cmake")
    set_tests_properties(rd_date_selftest_uber
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                    ENVIRONMENT "${_env_date_stencil};ICO_RD_UBER=on")
    # the shadow count's self-test (rd_shadow_selftest.c): the one draw
    # equal to the seven draws byte for byte at both scene sizes of the
    # tool (a wide scene at scale 2 with Full pixel's left-out texels, then
    # another size), every band centre the count's colour, and the line that
    # says how the stencil is read (four texels a gather, or a texel a load
    # where the gathers' count differs); sabotaged (the
    # one draw's table wrong at m = 1), the one draw differs, the profile's
    # flag goes off and the seven draws resolve the count (no second line:
    # nothing to check then); [dev] shadow_one = false, no self-test and the
    # profile's words "shadow count seven draws ([dev])"
    add_test(NAME rd_shadow_selftest
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                     "-DPATH=one draw"
                     "-DRESULTS=equal to the seven draws over [0-9]+x[0-9]+, 142 band centres ok"
                     "-DEXTRA=profile: .*shadow count one draw"
                     "-DEXTRA2=shadow count: the stencil read (four texels a gather, equal to the seven draws byte for byte|a texel a load, as the gathers' count)"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_shadow_selftest.cmake")
    set_tests_properties(rd_shadow_selftest PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                       ENVIRONMENT "${_env}")
    # the same on a cropped frame (624 wide at 16:10, exact 614.40: the last
    # band's centre fell in the left-out columns and turned the one draw
    # off) and on one exactly its width (2048 at 48:9)
    foreach(_c IN ITEMS "cropped|624|448|1|0|0|0|0|0|1.6" "wide|2048|448|1|0|0|0|0|0|5.3333333")
      string(REPLACE "|" ";" _p "${_c}")
      list(GET _p 0 _n)
      list(REMOVE_AT _p 0)
      list(GET _p 0 _w)
      string(REPLACE ";" "|" _a "${_p}")
      add_test(NAME rd_shadow_selftest_${_n}
               COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                       "-DPATH=one draw" "-DARGS=${_a}"
                       # the tool runs the self-test twice (its first scene, then the
                       # one made at the asked size): every line one draw with its
                       # 142 centres ok, and the asked width's line present
                       "-DRESULTS=equal to the seven draws over [0-9]+x448, 142 band centres ok"
                       "-DEXTRA=shadow count: one draw \\(self-test: equal to the seven draws over ${_w}x448, 142 band centres ok"
                       -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_shadow_selftest.cmake")
      set_tests_properties(rd_shadow_selftest_${_n} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                           ENVIRONMENT "${_env}")
    endforeach()
    add_test(NAME rd_shadow_selftest_sabotage
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                     "-DPATH=seven draws"
                     "-DRESULTS=one draw differs at [0-9]+ of [0-9]+ texels, the first \\([0-9]+,[0-9]+\\): [0-9A-F]+ for [0-9A-F]+"
                     "-DEXTRA=profile: shadow count one draw off \\(self-test\\)"
                     "-DEXTRA2=shadow count: ICO_RD_SHADOW_SABOTAGE: "
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_shadow_selftest.cmake")
    set_tests_properties(rd_shadow_selftest_sabotage
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                    ENVIRONMENT "${_env};ICO_RD_SHADOW_SABOTAGE=1")
    # the store plan's keep (rd_store_plan_test.c; rd_store_plan_ab below
    # holds the drops byte for byte over the corpus)
    add_test(NAME rd_store_plan COMMAND rd_store_plan_test)
    set_tests_properties(rd_store_plan PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT "${_env}")
    # the passes cut (rd_pass_cut_test.c; rd_pass_cut_ab holds them byte for
    # byte over the corpus)
    add_test(NAME rd_pass_cut COMMAND rd_pass_cut_test)
    set_tests_properties(rd_pass_cut PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT "${_env}")
endif()

# The desktop's local reads and their start-up cost probe (rd_local_probe.c):
# the verdict's rule on synthetic times (rd_local_probe_rule, no device), then
# the probe on lavapipe (the desktop class, with local read) through
# rd_fog_selftest_test with ICO_RD_LOCAL_PROBE=1 (a headless run keeps the
# snapshots without it), the local reads' time biased by
# ICO_RD_LOCAL_PROBE_SABOTAGE so both verdicts run: fast, the local reads
# kept (the fog's depth path local at both scene sizes, "kept (probe x vs y
# ms)"); slow, turned off ("DATE stencil off (probe)", every named target
# made again, the fog's self-test run again: in place at both sizes)
if(ICO_RUN_TESTS)
    add_test(NAME rd_local_probe_rule COMMAND rd_local_probe_test)
    add_test(NAME rd_local_probe_kept
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                     -DFOG_PATH=local
                     "-DRESULTS=local ok [0-2], inplace not tried, copy not tried, buffer not tried"
                     "-DEXTRA=profile: local read on the desktop: kept \\(probe [0-9.]+ vs [0-9.]+ ms\\): "
                     "-DEXTRA2=profile: ICO_RD_LOCAL_PROBE_SABOTAGE: "
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_fog_selftest.cmake")
    set_tests_properties(rd_local_probe_kept
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                    ENVIRONMENT "${_env};ICO_RD_LOCAL_PROBE=1;ICO_RD_LOCAL_PROBE_SABOTAGE=fast")
    add_test(NAME rd_local_probe_off
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_fog_selftest_test>
                     -DFOG_PATH=inplace
                     "-DRESULTS=inplace ok [0-2], copy not tried, buffer not tried"
                     "-DEXTRA=profile: local read on the desktop: off \\(probe: slower, [0-9.]+ vs [0-9.]+ ms\\): "
                     "-DEXTRA2=profile: DATE stencil off \\(probe\\)"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_fog_selftest.cmake")
    set_tests_properties(rd_local_probe_off
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                    ENVIRONMENT "${_env};ICO_RD_LOCAL_PROBE=1;ICO_RD_LOCAL_PROBE_SABOTAGE=slow")
endif()
