# rd_replay_tool_jitter: rd_replay_tool's --jitter X,Y (port/render/rd_jitter.h,
# docs/port/RENDER.md "Jitter") on the dump rd_pixel leaves behind.
#   - --jitter outside -0.5..0.5, or not X,Y, exits 1 with its message (no
#     device needed)
#   - at the Original preset (1x) and at Enhanced 2x, --pass-dump with
#     --present 640x480 --stats four times: without --jitter, with
#     --jitter 0,0, with --jitter 0.25,0.25 and with ICO_RD_JITTER_TEST=0.25,0.25
#     in the environment ([dev] jitter_test's variable) and no --jitter:
#       * the run without says "stats: jitter off";
#       * 0,0 says "jitter 0, 0: N frame groups" with N > 0 (the jitter's
#         path ran: 0.0 added to the frame groups of SCENE's passes) and every
#         target's PNG and PRESENT.png equal the run without byte for byte;
#       * 0.25,0.25 says "jitter 0.25, 0.25: N frame groups" with N > 0 and
#         SCENE.png and PRESENT.png differ from the run without (the scene
#         moved a quarter texel right and down);
#       * the environment's run equals the 0.25,0.25 run byte for byte, every
#         PNG, and says the same jitter line.
#   Exit 77 (no device) skips the second half.
# -DTOOL=<rd_replay_tool> -DDUMP=<rddump> -DOUT=<a folder the test may write>
if(NOT EXISTS "${DUMP}")
  message(STATUS "SKIP: ${DUMP}: no dump (rd_pixel skipped)")
  return()
endif()
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")

foreach(bad "0.75,0" "0.25" "a,b" "0.1,0.2,0.3")
  execute_process(COMMAND "${TOOL}" "${DUMP}" "${OUT}/x.png" --jitter "${bad}" --no-device --list
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 1 OR NOT err MATCHES "--jitter: X,Y with each -0.5..0.5")
    message(FATAL_ERROR "--jitter ${bad}: exit ${rc}, want 1 and the message:\n${err}")
  endif()
endforeach()

set(names SCENE DISPLAY SHADOW0 SHADOW1 SHADOW2 WORK0 WORK1 WORK2 WORK3 AA0 AA1 FEED128
    AURA_WORK AURA_TAP WORK2_PAD FEED_HELD DISPLAY_HELD WORK3_HELD PRESENT)
set(args_orig --present 640x480)
set(args_enh2x --enhanced --aspect 4:3 --resolution 2x --present 640x480)
set(runs off zero quarter env)
set(jit_off "")
set(jit_zero --jitter 0,0)
set(jit_quarter --jitter 0.25,0.25)
set(jit_env "")
set(line_off "stats: jitter off")
set(line_zero "stats: jitter 0, 0: ([0-9]+) frame groups")
set(line_quarter "stats: jitter 0.25, 0.25: ([0-9]+) frame groups")
set(line_env "${line_quarter}")
set(failures 0)
foreach(s orig enh2x)
  foreach(run IN LISTS runs)
    set(dir "${OUT}/${s}-${run}")
    file(MAKE_DIRECTORY "${dir}")
    set(envArgs ICO_RD_JITTER_TEST=)
    if(run STREQUAL "env")
      set(envArgs ICO_RD_JITTER_TEST=0.25,0.25)
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${envArgs}
                            "${TOOL}" "${DUMP}" - --pass-dump "${dir}" --stats ${args_${s}}
                            ${jit_${run}}
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(rc EQUAL 77)
      message(STATUS "SKIP: no device: ${err}")
      return()
    endif()
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "${s} ${run}: exit ${rc}:\n${out}\n${err}")
    endif()
    string(REPLACE ";" "," flat "${out}")
    if(NOT flat MATCHES "${line_${run}}")
      message(SEND_ERROR "${s} ${run}: no \"${line_${run}}\" line:\n${out}")
      math(EXPR failures "${failures} + 1")
    elseif(NOT run STREQUAL "off" AND NOT CMAKE_MATCH_1 GREATER 0)
      message(SEND_ERROR "${s} ${run}: the jitter moved no frame group:\n${out}")
      math(EXPR failures "${failures} + 1")
    endif()
    foreach(n IN LISTS names)
      if(NOT EXISTS "${dir}/${n}.png")
        message(FATAL_ERROR "${s} ${run}: --pass-dump wrote no ${n}.png:\n${out}\n${err}")
      endif()
    endforeach()
  endforeach()
  # 0,0 is the picture without the jitter, every target and the present
  foreach(n IN LISTS names)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUT}/${s}-off/${n}.png"
                            "${OUT}/${s}-zero/${n}.png" RESULT_VARIABLE same)
    if(NOT same EQUAL 0)
      message(SEND_ERROR "${s}: ${n}.png differs between no jitter and --jitter 0,0")
      math(EXPR failures "${failures} + 1")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUT}/${s}-quarter/${n}.png"
                            "${OUT}/${s}-env/${n}.png" RESULT_VARIABLE same)
    if(NOT same EQUAL 0)
      message(SEND_ERROR "${s}: ${n}.png differs between --jitter 0.25,0.25 and "
                         "ICO_RD_JITTER_TEST=0.25,0.25")
      math(EXPR failures "${failures} + 1")
    endif()
  endforeach()
  # a quarter texel moves the scene, and so the presented picture
  foreach(n SCENE PRESENT)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUT}/${s}-off/${n}.png"
                            "${OUT}/${s}-quarter/${n}.png" RESULT_VARIABLE same)
    if(same EQUAL 0)
      message(SEND_ERROR "${s}: ${n}.png is the same with --jitter 0.25,0.25 as without")
      math(EXPR failures "${failures} + 1")
    endif()
  endforeach()
  message(STATUS "${s}: 0,0 equal to no jitter, 0.25,0.25 moved SCENE and the present")
endforeach()
if(failures GREATER 0)
  message(FATAL_ERROR "rd_replay_tool_jitter: ${failures} failures")
endif()
message(STATUS "rd_replay_tool_jitter: ok")
